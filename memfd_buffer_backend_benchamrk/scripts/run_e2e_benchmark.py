#!/usr/bin/env python3
"""Run old memfd backend CPU/memfd pub/sub latency comparisons."""

import argparse
import csv
import os
import subprocess
import sys
import time


RESULT_FIELDS = [
    "communication",
    "backend",
    "size_bytes",
    "received",
    "measured",
    "va_matches",
    "p50_us",
    "p95_us",
    "p99_us",
]


def command(role, mode, size, count, rate):
    return [
        "ros2",
        "run",
        "memfd_buffer_backend_benchmark",
        "memfd_buffer_backend_e2e_node",
        "--role",
        role,
        "--mode",
        mode,
        "--size",
        str(size),
        "--count",
        str(count),
        "--rate-hz",
        str(rate),
    ]


def parse_result(output, communication, mode, size):
    lines = [line for line in output.splitlines() if line.startswith("RESULT,")]
    if not lines:
        raise RuntimeError(
            f"no result: {communication}, {mode}, {size}; process output: {output}"
        )

    fields = next(csv.reader([lines[-1]]))
    if len(fields) != 11 or fields[0] != "RESULT" or fields[1] != "ok":
        raise RuntimeError(f"benchmark error: {lines[-1]}")

    result = dict(zip(RESULT_FIELDS, fields[2:]))
    if (
        result["communication"] != communication
        or result["backend"] != mode
        or int(result["size_bytes"]) != size
    ):
        raise RuntimeError(f"unexpected result: {lines[-1]}")
    return result


def terminate_process(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def run_inter_process_case(mode, size, count, rate, env):
    communication = "inter_process"
    subscriber = subprocess.Popen(
        command("sub", mode, size, count, rate),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    publisher = None
    try:
        time.sleep(1.0)
        publisher = subprocess.Popen(
            command("pub", mode, size, count, rate),
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        try:
            output, _ = subscriber.communicate(timeout=(count / rate) + 30.0)
        except subprocess.TimeoutExpired:
            subscriber.kill()
            output, _ = subscriber.communicate()
            raise RuntimeError(
                f"timeout: {communication}, {mode}, {size}; subscriber output: {output}"
            )
    finally:
        terminate_process(publisher)
    return parse_result(output, communication, mode, size)


def run_intra_process_case(mode, size, count, rate, env):
    communication = "intra_process_va"
    process = subprocess.Popen(
        command("intra", mode, size, count, rate),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    try:
        output, _ = process.communicate(timeout=(count / rate) + 30.0)
    except subprocess.TimeoutExpired:
        process.kill()
        output, _ = process.communicate()
        raise RuntimeError(
            f"timeout: {communication}, {mode}, {size}; process output: {output}"
        )
    if process.returncode != 0:
        raise RuntimeError(
            f"process failed: {communication}, {mode}, {size}, returncode={process.returncode}; "
            f"output: {output}"
        )
    return parse_result(output, communication, mode, size)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="memfd-old-pubsub-results.csv")
    parser.add_argument(
        "--sizes",
        default="64,1024,4096,16384,65536,262144,1048576,4194304,16777216",
    )
    parser.add_argument("--count", type=int, default=30)
    parser.add_argument("--rate-hz", type=int, default=10)
    args = parser.parse_args()

    if args.count <= 10:
        parser.error("--count must be greater than the fixed 10-sample warm-up")
    if args.rate_hz <= 0:
        parser.error("--rate-hz must be positive")

    env = os.environ.copy()
    env["RMW_IMPLEMENTATION"] = "rmw_fastrtps_cpp"
    sizes = [int(value) for value in args.sizes.split(",") if value]
    rows = []
    for size in sizes:
        for mode in ("cpu", "memfd"):
            for communication in ("inter_process", "intra_process_va"):
                print(
                    f"running {communication} {mode} {size} B",
                    file=sys.stderr,
                    flush=True,
                )
                if communication == "inter_process":
                    result = run_inter_process_case(mode, size, args.count, args.rate_hz, env)
                else:
                    result = run_intra_process_case(mode, size, args.count, args.rate_hz, env)
                if communication == "intra_process_va" and int(result["va_matches"]) != args.count:
                    raise RuntimeError(f"intra-process VA sharing failed: {result}")
                rows.append(result)

    with open(args.output, "w", newline="", encoding="utf-8") as output_file:
        writer = csv.DictWriter(output_file, fieldnames=RESULT_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
