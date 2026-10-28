# TCP Connect Port Scanner

A small, reliable IPv4 TCP port scanner written in C for Linux and WSL.

It combines non-blocking sockets with a bounded worker pool, configurable
timeouts, and deterministic CSV output. The goal is practical exposure checking
without growing into a vulnerability-scanning framework or an Nmap replacement.

> [!IMPORTANT]
> Scan only systems you own or are explicitly authorized to test. Unauthorized
> scanning may violate network policies or local law and can trigger security
> monitoring.

## Highlights

- Scans a user-selected IPv4 TCP port range
- Uses non-blocking `connect()`, `poll()`, and `SO_ERROR`
- Limits concurrency instead of creating unlimited threads or sockets
- Supports configurable timeouts and worker counts
- Produces stable, port-sorted CSV with UTC timestamps
- Keeps diagnostics on `stderr` and scan data on `stdout`
- Handles `Ctrl+C` gracefully and returns completed results
- Includes strict builds, loopback integration tests, sanitizers, and Linux CI

## Quick start

```bash
git clone https://github.com/NotLearner12/port-scanner.git
cd port-scanner
make
./scanner 127.0.0.1 1 1024
```

Requirements:

- Linux or WSL
- GCC, Clang, or another C11 compiler
- POSIX threads
- Python 3 only when running the test suite

## Usage

```text
./scanner <IPv4> <START_PORT> <END_PORT> [--timeout MS] [--concurrency N]
```

| Argument | Accepted value | Default |
| --- | --- | --- |
| `IPv4` | Numeric IPv4 address | Required |
| `START_PORT` | `1-65535` | Required |
| `END_PORT` | `1-65535` and not below `START_PORT` | Required |
| `--timeout` | `100-5000` milliseconds | `1000` |
| `--concurrency` | `1-512` workers | `100` |

Show the command summary:

```bash
./scanner --help
```

### Examples

Scan the first 1,024 TCP ports on localhost:

```bash
./scanner 127.0.0.1 1 1024
```

Use a shorter timeout and 150 workers on an authorized LAN target:

```bash
./scanner 192.168.1.10 1 65535 --timeout 750 --concurrency 150
```

Save clean CSV while leaving errors visible in the terminal:

```bash
./scanner 192.168.1.10 20 443 > results.csv
```

## Output

Results are written to `stdout` in port order:

```csv
timestamp,ip,port,state
2026-03-16T09:25:05Z,127.0.0.1,22,open
2026-03-16T09:25:05Z,127.0.0.1,23,closed
```

| State | Meaning |
| --- | --- |
| `open` | The TCP connection completed successfully. |
| `closed` | The destination actively refused the connection. |
| `filtered` | No result arrived before the configured timeout. |
| `unreachable` | The operating system reported the host or network unreachable. |
| `error` | A local or otherwise unclassified socket error occurred. |

A timeout is evidence that the target remained silent. It is not proof that a
firewall caused the silence.

Exit codes:

| Code | Meaning |
| --- | --- |
| `0` | The scan completed. |
| `1` | Input validation, setup, or runtime failure. |
| `130` | The scan was interrupted; CSV contains completed ports only. |

## How it works

For every assigned port, a worker:

1. Creates a TCP socket.
2. Preserves its existing flags and enables non-blocking mode.
3. Calls `connect()` using the validated target address.
4. Waits with `poll()` until the connection finishes or the deadline expires.
5. Reads `SO_ERROR`, classifies the result, and closes the socket.

Workers claim ports from a shared queue. Results are stored in memory and
printed only after the workers finish, which keeps the CSV complete and sorted
even though connections run concurrently.

## Performance

At most the configured number of ports are in flight. For completely silent
ports, a useful upper-bound estimate is:

```text
ceil(number_of_ports / concurrency) × timeout
```

With the defaults, 1,000 silent ports take roughly ten seconds plus scheduling
overhead. Closed ports normally finish much faster because the refusal arrives
immediately.

More concurrency is not automatically better. Excessive values can exhaust
local file descriptors, ephemeral ports, or network equipment. Start with the
defaults and tune only on networks you control.

## Build and test

Build with strict warnings:

```bash
make clean all
```

Equivalent compiler command:

```bash
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    port_scanner.c -o scanner -pthread
```

Run the loopback-only integration tests:

```bash
make test
```

Run the same tests with AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
make sanitizer
```

The tests cover argument validation, correct target-address use, known open and
closed loopback ports, CSV structure, completeness, and deterministic ordering.
They intentionally do not depend on external hosts. Timeout classification is
network-dependent and is not asserted against the public internet.

## Intentional v1.0 boundaries

This release supports:

- One numeric IPv4 target per run
- TCP connect scans
- One inclusive port range
- CSV output

This release intentionally does **not** include:

- UDP or raw SYN scanning
- CIDR ranges, host discovery, or IPv6
- Banner grabbing or service/version detection
- Vulnerability detection or exploitation
- A GUI, database, plugin system, or distributed workers

These boundaries are the finish line, not missing promises. They keep the
project understandable, testable, and useful as a focused scanner.

## License

Released under the [MIT License](LICENSE).
