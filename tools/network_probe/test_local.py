"""Run a two-peer loopback smoke test for RecubinNetworkProbe."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time


READY_LINE = "[probe] READY expected-peers=2"
CONNECTED_PEER = re.compile(r"\[probe\] connected peer=(\d+)\b")


class ProbeProcess:
    def __init__(self, name: str, command: list[str]) -> None:
        self.name = name
        self.lines: list[str] = []
        creationflags = 0
        popen_options: dict[str, object] = {}
        if os.name == "nt":
            creationflags = subprocess.CREATE_NEW_PROCESS_GROUP
        else:
            popen_options["start_new_session"] = True
        self.process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            errors="replace",
            bufsize=1,
            creationflags=creationflags,
            **popen_options,
        )
        self.reader = threading.Thread(target=self._read_output, daemon=True)
        self.reader.start()

    def _read_output(self) -> None:
        assert self.process.stdout is not None
        for line in self.process.stdout:
            self.lines.append(line.rstrip("\r\n"))

    def output(self) -> str:
        return "\n".join(self.lines)

    def stop(self) -> None:
        if self.process.poll() is not None:
            self.reader.join(timeout=1)
            return
        try:
            if os.name == "nt":
                self.process.terminate()
            else:
                os.killpg(self.process.pid, signal.SIGTERM)
            self.process.wait(timeout=3)
        except (ProcessLookupError, subprocess.TimeoutExpired):
            if self.process.poll() is None:
                if os.name == "nt":
                    self.process.kill()
                else:
                    os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=3)
        finally:
            self.reader.join(timeout=1)


def choose_loopback_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        if os.name == "nt":
            reservation.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        reservation.bind(("127.0.0.1", 0))
        return int(reservation.getsockname()[1])


def resolve_probe(value: str) -> str:
    candidate = Path(value)
    if candidate.exists():
        return str(candidate.resolve())
    discovered = shutil.which(value)
    if discovered is not None:
        return discovered
    raise FileNotFoundError(f"network probe not found: {value}")


def peer_id(output: str) -> str | None:
    match = CONNECTED_PEER.search(output)
    return match.group(1) if match else None


def print_logs(processes: list[ProbeProcess]) -> None:
    for probe in processes:
        print(f"--- {probe.name} log ---", file=sys.stderr)
        print(probe.output() or "<no output>", file=sys.stderr)


def run_test(executable: str, timeout: float) -> int:
    port = choose_loopback_udp_port()
    common = [
        "--duration",
        "10",
        "--message-interval",
        "1",
        "--expect-peers",
        "2",
    ]
    processes: list[ProbeProcess] = []
    try:
        host = ProbeProcess(
            "host", [executable, "--direct-host", str(port), *common]
        )
        processes.append(host)
        time.sleep(0.25)
        if host.process.poll() is not None:
            raise RuntimeError(f"host exited early with code {host.process.returncode}")

        client = ProbeProcess(
            "client",
            [
                executable,
                "--direct-connect",
                f"127.0.0.1:{port}",
                "--listen-port",
                "0",
                *common,
            ],
        )
        processes.append(client)

        deadline = time.monotonic() + timeout
        for probe in processes:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"network probe test exceeded {timeout:g} seconds")
            try:
                probe.process.wait(timeout=remaining)
            except subprocess.TimeoutExpired as error:
                raise TimeoutError(
                    f"network probe test exceeded {timeout:g} seconds"
                ) from error
        for probe in processes:
            probe.reader.join(timeout=1)

        failures: list[str] = []
        for probe in processes:
            if probe.process.returncode != 0:
                failures.append(
                    f"{probe.name} exited with code {probe.process.returncode}"
                )
            if READY_LINE not in probe.output():
                failures.append(f"{probe.name} did not report {READY_LINE!r}")

        host_id = peer_id(host.output())
        client_id = peer_id(client.output())
        if host_id is None:
            failures.append("host did not report its peer id")
        if client_id is None:
            failures.append("client did not report its peer id")
        if client_id is not None and not re.search(
            rf"\[probe\] chat peer={re.escape(client_id)} "
            rf"text=network-probe-{re.escape(client_id)}-\d+\b",
            host.output(),
        ):
            failures.append("host did not receive a client chat message")
        if host_id is not None and not re.search(
            rf"\[probe\] chat peer={re.escape(host_id)} "
            rf"text=network-probe-{re.escape(host_id)}-\d+\b",
            client.output(),
        ):
            failures.append("client did not receive a host chat message")

        if failures:
            raise RuntimeError("; ".join(failures))
        print(
            f"network probe loopback test passed "
            f"(host peer={host_id}, client peer={client_id})"
        )
        return 0
    except (FileNotFoundError, OSError, RuntimeError, TimeoutError) as error:
        for probe in reversed(processes):
            probe.stop()
        print(f"network probe loopback test failed: {error}", file=sys.stderr)
        print_logs(processes)
        return 1
    finally:
        for probe in reversed(processes):
            probe.stop()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a direct two-peer RecubinNetworkProbe loopback test"
    )
    parser.add_argument("probe", help="path to RecubinNetworkProbe executable")
    parser.add_argument(
        "--timeout", type=float, default=25.0, help="overall timeout in seconds"
    )
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be greater than zero")
    try:
        executable = resolve_probe(args.probe)
    except FileNotFoundError as error:
        parser.error(str(error))
    return run_test(executable, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())
