# TCP Connect Port Scanner

> A focused IPv4 TCP connect scanner written in C for Linux and WSL. It uses a bounded worker pool, non-blocking sockets, and deterministic CSV output for authorized exposure checks.

[![CI](https://github.com/NotLearner12/port-scanner/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/NotLearner12/port-scanner/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Language: C](https://img.shields.io/badge/language-C-00599C.svg)](port_scanner.c)

**C11** · **Linux / WSL** · **TCP connect scanning** · **CSV output** · **MIT licensed**

## Overview

This project is deliberately small and dependable. Give it one numeric IPv4 address and an inclusive TCP port range; it connects to each port with a configurable timeout and produces sorted CSV results.

It is intended for systems you own or are explicitly authorized to assess—not as a vulnerability scanner or replacement for Nmap.

> [!IMPORTANT]
> Scan only hosts and networks you own or have explicit permission to test. Unauthorized scanning can violate policy or law and may trigger security monitoring.

## Features

| Feature | Status | Details |
| --- | :---: | --- |
| IPv4 TCP connect scans | ✅ Available | Scan one numeric IPv4 target and an inclusive port range. |
| Bounded concurrency | ✅ Available | Uses 1-512 workers; default is 100. |
| Timeout control | ✅ Available | Configure per-port timeout from 100-5000 ms; default is 1000 ms. |
| Stable CSV results | ✅ Available | UTC timestamps; results are emitted in ascending port order. |
| Result classification | ✅ Available | Reports open, closed, filtered, unreachable, or error. |
| Graceful interruption | ✅ Available | Ctrl+C preserves results already completed. |
| Automated verification | ✅ Available | Loopback integration tests, sanitizers, and GitHub Actions CI. |
| UDP, SYN, CIDR, IPv6, banners | ❌ Out of scope | Kept out intentionally to retain a clear v1.0 boundary. |

## Tech stack

| Area | Technology |
| --- | --- |
| Scanner | C11 |
| Networking | POSIX sockets, connect(), poll(), SO_ERROR |
| Concurrency | POSIX threads with a shared work queue |
| Build | Make + a C11 compiler |
| Tests | Python 3 unittest, loopback sockets only |
| Quality checks | Strict compiler warnings, AddressSanitizer, UndefinedBehaviorSanitizer |
| Automation | GitHub Actions on Ubuntu |

## Architecture

~~~text
┌───────────────────────────────────────────────────────────────────┐
│                         scanner executable                        │
│                                                                   │
│  CLI validation ──► shared port queue ──► bounded worker pool     │
│                              │                    │              │
│                              │                    ▼              │
│                              │      non-blocking TCP connect      │
│                              │      poll() + SO_ERROR             │
│                              ▼                                   │
│                    in-memory result array                         │
│                              │                                   │
│                              ▼                                   │
│                 deterministic, port-sorted CSV on stdout          │
└───────────────────────────────────────────────────────────────────┘
~~~

Each worker claims one port at a time, opens a non-blocking socket, waits until the connection completes or its deadline expires, classifies the result, and closes the socket. Results are collected first and printed after all workers finish, so concurrent work never scrambles the CSV order.

## Project layout

~~~text
.
├── port_scanner.c           # Scanner implementation
├── tests/
│   └── test_scanner.py      # Loopback integration tests
├── .github/workflows/ci.yml # Build and test workflow
├── Makefile                 # Build, test, sanitizer, and clean commands
├── README.md                # Project documentation
└── LICENSE                  # MIT License
~~~

## Getting started

### Prerequisites

- Linux or WSL
- GCC, Clang, or another C11 compiler
- POSIX threads (available on standard Linux/WSL toolchains)
- Python 3 only for the test suite

### Build and run

~~~bash
git clone https://github.com/NotLearner12/port-scanner.git
cd port-scanner
make
./scanner 127.0.0.1 1 1024
~~~

Show the CLI summary at any time:

~~~bash
./scanner --help
~~~

## Usage

~~~text
./scanner <IPv4> <START_PORT> <END_PORT> [--timeout MS] [--concurrency N]
~~~

| Argument | Accepted value | Default |
| --- | --- | --- |
| IPv4 | Numeric IPv4 address | Required |
| START_PORT | 1-65535 | Required |
| END_PORT | 1-65535, not below START_PORT | Required |
| --timeout | 100-5000 milliseconds | 1000 |
| --concurrency | 1-512 workers | 100 |

### Examples

Scan the first 1,024 TCP ports on localhost:

~~~bash
./scanner 127.0.0.1 1 1024
~~~

Use a shorter timeout and 150 workers against an authorized LAN host:

~~~bash
./scanner 192.168.1.10 1 65535 --timeout 750 --concurrency 150
~~~

Write clean CSV to a file while retaining diagnostics in the terminal:

~~~bash
./scanner 192.168.1.10 20 443 > results.csv
~~~

## Output and exit codes

The scanner writes only results to stdout, in ascending port order:

~~~csv
timestamp,ip,port,state
2026-03-16T09:25:05Z,127.0.0.1,22,open
2026-03-16T09:25:05Z,127.0.0.1,23,closed
~~~

| State | Meaning |
| --- | --- |
| open | The TCP connection completed successfully. |
| closed | The destination actively refused the connection. |
| filtered | No result arrived before the timeout. This does not prove a firewall caused it. |
| unreachable | The operating system reported a host or network unreachable condition. |
| error | A local or unclassified socket error occurred. |

| Exit code | Meaning |
| --- | --- |
| 0 | Scan completed. |
| 1 | Input validation, setup, or runtime failure. |
| 130 | Interrupted; CSV contains only completed ports. |

## Performance model

At most the selected number of ports are in flight. For silent ports, a practical upper-bound estimate is:

~~~text
ceil(number of ports / concurrency) × timeout
~~~

With the default settings, 1,000 silent ports take roughly ten seconds plus scheduling overhead. Increasing concurrency is not always faster: excessive values can exhaust local file descriptors, ephemeral ports, or network equipment.

## Quality and verification

~~~bash
# Strict production build
make clean all

# Loopback-only integration tests
make test

# Run the same tests with AddressSanitizer and UndefinedBehaviorSanitizer
make sanitizer
~~~

The tests validate CLI errors, correct target-address handling, known open and closed loopback ports, CSV structure, complete output, and deterministic port ordering. They do not scan external hosts.

GitHub Actions runs the strict build, integration tests, and sanitizer suite on pushes to main and pull requests.

## Troubleshooting

| Issue | What to check |
| --- | --- |
| make cannot find a compiler | Install a Linux/WSL C toolchain such as GCC or Clang. |
| python3 is missing during make test | Install Python 3; it is used only by the test harness. |
| All ports are filtered | Confirm the target is authorized and reachable; a timeout only means no response arrived in time. |
| Scan is slower than expected | Reduce the scanned range, adjust the timeout, or carefully tune concurrency on your own network. |
| Permission or policy warning | Stop and obtain authorization before scanning. |

## Scope and contribution

The v1.0 finish line is one IPv4 target, TCP connect scanning, one inclusive port range, and CSV output. Features such as UDP/SYN scans, CIDR ranges, host discovery, IPv6, banner grabbing, vulnerability detection, GUIs, databases, plugins, and distributed workers are intentionally excluded.

Focused improvements are welcome: preserve the C11/Linux boundary, keep tests passing, and update this README when public behavior changes. Please open a pull request with a clear description and validation notes.

## License

Released under the [MIT License](LICENSE). Copyright © 2026 Anay Sharma.
