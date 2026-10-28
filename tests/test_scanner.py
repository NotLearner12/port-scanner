import csv
import io
import os
import re
import socket
import subprocess
import unittest


SCANNER = os.environ.get("SCANNER", "./scanner")


def run_scanner(*arguments, timeout=10):
    return subprocess.run(
        [SCANNER, *map(str, arguments)],
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )


def parse_csv(output):
    return list(csv.DictReader(io.StringIO(output)))


class ScannerTests(unittest.TestCase):
    def test_help(self):
        result = run_scanner("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Usage:", result.stdout)

    def test_rejects_invalid_ipv4(self):
        result = run_scanner("not-an-ip", 80, 80)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid IPv4 address", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_rejects_invalid_port_inputs(self):
        invalid_ranges = [
            ("abc", "80"),
            ("0", "80"),
            ("80", "65536"),
            ("100", "80"),
        ]
        for start, end in invalid_ranges:
            with self.subTest(start=start, end=end):
                result = run_scanner("127.0.0.1", start, end)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Invalid port range", result.stderr)

    def test_rejects_invalid_options(self):
        cases = [
            ("--timeout", "99"),
            ("--timeout", "5001"),
            ("--concurrency", "0"),
            ("--concurrency", "513"),
            ("--unknown", "1"),
        ]
        for option, value in cases:
            with self.subTest(option=option, value=value):
                result = run_scanner("127.0.0.1", 80, 80, option, value)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")

    def test_reports_open_port_on_requested_address(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(("127.0.0.2", 0))
            listener.listen(1)
            port = listener.getsockname()[1]

            result = run_scanner(
                "127.0.0.2", port, port, "--timeout", 500, "--concurrency", 1
            )

        self.assertEqual(result.returncode, 0, result.stderr)
        rows = parse_csv(result.stdout)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["ip"], "127.0.0.2")
        self.assertEqual(int(rows[0]["port"]), port)
        self.assertEqual(rows[0]["state"], "open")
        self.assertRegex(
            rows[0]["timestamp"],
            re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$"),
        )

    def test_reports_closed_port(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as temporary:
            temporary.bind(("127.0.0.1", 0))
            port = temporary.getsockname()[1]

        result = run_scanner("127.0.0.1", port, port, "--timeout", 500)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = parse_csv(result.stdout)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["state"], "closed")

    def test_output_is_complete_and_port_sorted(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as temporary:
            temporary.bind(("127.0.0.1", 0))
            base_port = temporary.getsockname()[1]

        start = max(1, base_port - 2)
        end = min(65535, base_port + 2)
        result = run_scanner(
            "127.0.0.1", start, end, "--timeout", 500, "--concurrency", 5
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        rows = parse_csv(result.stdout)
        ports = [int(row["port"]) for row in rows]
        self.assertEqual(ports, list(range(start, end + 1)))
        self.assertTrue(all(len(row) == 4 for row in rows))


if __name__ == "__main__":
    unittest.main()
