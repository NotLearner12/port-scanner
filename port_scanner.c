#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_TIMEOUT_MS 1000
#define DEFAULT_CONCURRENCY 100
#define MIN_TIMEOUT_MS 100
#define MAX_TIMEOUT_MS 5000
#define MIN_CONCURRENCY 1
#define MAX_CONCURRENCY 512

enum port_result {
    PORT_NOT_SCANNED = 0,
    PORT_OPEN,
    PORT_CLOSED,
    PORT_FILTERED,
    PORT_UNREACHABLE,
    PORT_ERROR
};

struct scan_context {
    struct in_addr address;
    int start_port;
    int end_port;
    int next_port;
    int timeout_ms;
    enum port_result *results;
    pthread_mutex_t job_mutex;
};

static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

static void print_usage(FILE *stream, const char *program) {
    fprintf(stream,
            "Usage: %s <IPv4> <START_PORT> <END_PORT> "
            "[--timeout MS] [--concurrency N]\n",
            program);
}

static int parse_long_in_range(const char *text, long minimum, long maximum,
                               long *value) {
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < minimum ||
        parsed > maximum) {
        return -1;
    }

    *value = parsed;
    return 0;
}

static int64_t monotonic_milliseconds(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }

    return ((int64_t)now.tv_sec * 1000) + (now.tv_nsec / 1000000);
}

static enum port_result classify_error(int error_number) {
    switch (error_number) {
        case 0:
            return PORT_OPEN;
        case ECONNREFUSED:
            return PORT_CLOSED;
        case ETIMEDOUT:
            return PORT_FILTERED;
        case EHOSTUNREACH:
        case ENETUNREACH:
            return PORT_UNREACHABLE;
        default:
            return PORT_ERROR;
    }
}

static enum port_result scan_port(struct in_addr address, int port,
                                  int timeout_ms) {
    struct sockaddr_in target = {0};
    struct pollfd descriptor;
    int socket_fd;
    int flags;
    int connection_result;
    int saved_errno;
    int socket_error = 0;
    socklen_t socket_error_length = sizeof(socket_error);
    int64_t deadline;

    socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd < 0) {
        return PORT_ERROR;
    }

    flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(socket_fd);
        return PORT_ERROR;
    }

    target.sin_family = AF_INET;
    target.sin_port = htons((uint16_t)port);
    target.sin_addr = address;

    connection_result = connect(socket_fd, (struct sockaddr *)&target,
                                sizeof(target));
    if (connection_result == 0) {
        close(socket_fd);
        return PORT_OPEN;
    }

    saved_errno = errno;
    if (saved_errno != EINPROGRESS) {
        close(socket_fd);
        return classify_error(saved_errno);
    }

    descriptor.fd = socket_fd;
    descriptor.events = POLLOUT;
    descriptor.revents = 0;
    deadline = monotonic_milliseconds();
    if (deadline < 0) {
        close(socket_fd);
        return PORT_ERROR;
    }
    deadline += timeout_ms;

    for (;;) {
        int poll_result;
        int64_t now = monotonic_milliseconds();
        int remaining;

        if (now < 0) {
            close(socket_fd);
            return PORT_ERROR;
        }
        if (now >= deadline) {
            close(socket_fd);
            return PORT_FILTERED;
        }

        remaining = (int)(deadline - now);
        poll_result = poll(&descriptor, 1, remaining);
        if (poll_result > 0) {
            break;
        }
        if (poll_result == 0) {
            close(socket_fd);
            return PORT_FILTERED;
        }
        if (errno != EINTR) {
            close(socket_fd);
            return PORT_ERROR;
        }
        if (stop_requested) {
            close(socket_fd);
            return PORT_NOT_SCANNED;
        }
    }

    if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error,
                   &socket_error_length) < 0) {
        close(socket_fd);
        return PORT_ERROR;
    }

    close(socket_fd);
    return classify_error(socket_error);
}

static void *worker_main(void *argument) {
    struct scan_context *context = argument;

    while (!stop_requested) {
        int port;

        if (pthread_mutex_lock(&context->job_mutex) != 0) {
            stop_requested = 1;
            break;
        }

        if (context->next_port > context->end_port) {
            pthread_mutex_unlock(&context->job_mutex);
            break;
        }

        port = context->next_port;
        context->next_port++;
        pthread_mutex_unlock(&context->job_mutex);

        context->results[port - context->start_port] =
            scan_port(context->address, port, context->timeout_ms);
    }

    return NULL;
}

static const char *result_name(enum port_result result) {
    switch (result) {
        case PORT_OPEN:
            return "open";
        case PORT_CLOSED:
            return "closed";
        case PORT_FILTERED:
            return "filtered";
        case PORT_UNREACHABLE:
            return "unreachable";
        case PORT_ERROR:
            return "error";
        case PORT_NOT_SCANNED:
        default:
            return "not-scanned";
    }
}

static int format_timestamp(char *buffer, size_t buffer_size) {
    time_t current_time = time(NULL);
    struct tm utc_time;

    if (current_time == (time_t)-1 ||
        gmtime_r(&current_time, &utc_time) == NULL) {
        return -1;
    }

    return strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%SZ", &utc_time) > 0
               ? 0
               : -1;
}

int main(int argc, char *argv[]) {
    struct scan_context context;
    struct sigaction signal_action = {0};
    pthread_t *threads = NULL;
    enum port_result *results = NULL;
    long parsed_start;
    long parsed_end;
    long parsed_option;
    int timeout_ms = DEFAULT_TIMEOUT_MS;
    int concurrency = DEFAULT_CONCURRENCY;
    int total_ports;
    int worker_count;
    int created_workers = 0;
    int creation_failed = 0;
    int exit_code = 0;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(stdout, argv[0]);
        return 0;
    }

    if (argc < 4) {
        print_usage(stderr, argv[0]);
        return 1;
    }

    if (parse_long_in_range(argv[2], 1, 65535, &parsed_start) != 0 ||
        parse_long_in_range(argv[3], 1, 65535, &parsed_end) != 0 ||
        parsed_start > parsed_end) {
        fprintf(stderr, "Invalid port range: expected 1-65535 and START <= END.\n");
        return 1;
    }

    memset(&context, 0, sizeof(context));
    if (inet_pton(AF_INET, argv[1], &context.address) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", argv[1]);
        return 1;
    }

    for (int index = 4; index < argc; index += 2) {
        if (index + 1 >= argc) {
            fprintf(stderr, "Missing value for option: %s\n", argv[index]);
            return 1;
        }

        if (strcmp(argv[index], "--timeout") == 0) {
            if (parse_long_in_range(argv[index + 1], MIN_TIMEOUT_MS,
                                    MAX_TIMEOUT_MS, &parsed_option) != 0) {
                fprintf(stderr, "Timeout must be between %d and %d ms.\n",
                        MIN_TIMEOUT_MS, MAX_TIMEOUT_MS);
                return 1;
            }
            timeout_ms = (int)parsed_option;
        } else if (strcmp(argv[index], "--concurrency") == 0) {
            if (parse_long_in_range(argv[index + 1], MIN_CONCURRENCY,
                                    MAX_CONCURRENCY, &parsed_option) != 0) {
                fprintf(stderr, "Concurrency must be between %d and %d.\n",
                        MIN_CONCURRENCY, MAX_CONCURRENCY);
                return 1;
            }
            concurrency = (int)parsed_option;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[index]);
            return 1;
        }
    }

    context.start_port = (int)parsed_start;
    context.end_port = (int)parsed_end;
    context.next_port = context.start_port;
    context.timeout_ms = timeout_ms;
    total_ports = context.end_port - context.start_port + 1;
    worker_count = concurrency < total_ports ? concurrency : total_ports;

    results = calloc((size_t)total_ports, sizeof(*results));
    threads = calloc((size_t)worker_count, sizeof(*threads));
    if (results == NULL || threads == NULL) {
        fprintf(stderr, "Unable to allocate scanner resources.\n");
        free(results);
        free(threads);
        return 1;
    }
    context.results = results;

    if (pthread_mutex_init(&context.job_mutex, NULL) != 0) {
        fprintf(stderr, "Unable to initialize worker synchronization.\n");
        free(results);
        free(threads);
        return 1;
    }

    signal_action.sa_handler = handle_signal;
    sigemptyset(&signal_action.sa_mask);
    if (sigaction(SIGINT, &signal_action, NULL) != 0 ||
        sigaction(SIGTERM, &signal_action, NULL) != 0) {
        fprintf(stderr, "Unable to install signal handlers.\n");
        pthread_mutex_destroy(&context.job_mutex);
        free(results);
        free(threads);
        return 1;
    }

    for (int index = 0; index < worker_count; index++) {
        int create_result =
            pthread_create(&threads[index], NULL, worker_main, &context);
        if (create_result != 0) {
            fprintf(stderr, "Unable to create worker: %s\n",
                    strerror(create_result));
            creation_failed = 1;
            stop_requested = 1;
            break;
        }
        created_workers++;
    }

    for (int index = 0; index < created_workers; index++) {
        pthread_join(threads[index], NULL);
    }

    if (!creation_failed) {
        printf("timestamp,ip,port,state\n");
        for (int index = 0; index < total_ports; index++) {
            char timestamp[32];

            if (results[index] == PORT_NOT_SCANNED) {
                continue;
            }
            if (format_timestamp(timestamp, sizeof(timestamp)) != 0) {
                fprintf(stderr, "Unable to generate UTC timestamp.\n");
                exit_code = 1;
                break;
            }
            printf("%s,%s,%d,%s\n", timestamp, argv[1],
                   context.start_port + index, result_name(results[index]));
        }
    } else {
        exit_code = 1;
    }

    if (stop_requested && !creation_failed) {
        fprintf(stderr, "Scan interrupted; CSV contains completed ports only.\n");
        exit_code = 130;
    }

    pthread_mutex_destroy(&context.job_mutex);
    free(results);
    free(threads);
    return exit_code;
}
