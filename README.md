# TCP Connect Port Scanner

A focused IPv4 TCP connect scanner written in C for Linux and WSL. It uses
non-blocking sockets, bounded concurrency and per-port timeouts, then emits
deterministic CSV suitable for scripts and basic monitoring.

This project intentionally stays small: it is a reliable TCP exposure checker,
not a vulnerability scanner or an Nmap replacement.

## Safety

Scan only systems you own or are explicitly authorized to test. Port scanning
can trigger monitoring systems and may violate network policies when performed
without permission.

## Features

- IPv4 TCP connect scanning
- Configurable port range, timeout and concurrency
- Bounded worker pool; no unbounded thread or socket creation
- Non-blocking connections using `poll()` and `SO_ERROR`
- Stable, port-sorted CSV output with UTC timestamps
- Graceful `Ctrl+C` handling with partial completed results
- Strict compiler warnings, integration tests and sanitizer checks

## Requirements

- Linux or WSL
- A C11 compiler such as GCC or Clang
- POSIX threads
- Python 3 for the test suite

## Build

```bash
make
```

Equivalent direct command:

```bash
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    port_scanner.c -o scanner -pthread
```

## Usage

```text
./scanner <IPv4> <START_PORT> <END_PORT> [--timeout MS] [--concurrency N]
```

Defaults:

- Timeout: `1000` ms
- Concurrency: `100`

Accepted limits:

- Ports: `1-65535`
- Timeout: `100-5000` ms
- Concurrency: `1-512`

Examples:

```bash
./scanner 127.0.0.1 1 1024
./scanner 192.168.1.10 1 65535 --timeout 750 --concurrency 150
./scanner 192.168.1.10 20 443 > results.csv
```

Diagnostics and input errors go to `stderr`, so redirected CSV on `stdout`
remains parseable.

## Output

```csv
timestamp,ip,port,state
2026-03-16T09:25:05Z,127.0.0.1,22,open
2026-03-16T09:25:05Z,127.0.0.1,23,closed
```

States:

| State | Meaning |
| --- | --- |
| `open` | The TCP connection completed successfully. |
| `closed` | The destination actively refused the connection. |
| `filtered` | No result arrived before the configured timeout. |
| `unreachable` | The operating system reported the host or network unreachable. |
| `error` | A local or otherwise unclassified socket error occurred. |

These states reflect TCP connect behavior. A timeout is evidence of silence, not
proof that a firewall is the cause.

## Tests

```bash
make test
make sanitizer
```

The integration tests use loopback addresses only. They verify input handling,
the requested target address, open and closed ports, CSV structure, completeness
and deterministic port ordering. Timeout classification is environment-dependent
and is therefore not asserted using an external host.

## Performance model

At most the configured number of ports are in flight. With the default
concurrency of 100 and a one-second timeout, 1,000 completely silent ports take
roughly ten seconds plus scheduling overhead. Closed ports usually complete much
faster because the refusal arrives immediately.

Higher concurrency is not always faster and can exhaust local or network
resources. Start with the defaults.

## Intentional v1.0 limits

- One numeric IPv4 address per run
- TCP connect scans only
- No UDP, raw SYN scans or privileged packet crafting
- No host discovery, CIDR ranges or IPv6
- No banner grabbing, service detection or vulnerability checks
- No GUI, database or distributed scanning

Keeping these boundaries makes the scanner understandable, testable and useful
without turning it into a large security framework.

## License

MIT
