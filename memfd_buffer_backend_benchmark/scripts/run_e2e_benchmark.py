#!/usr/bin/env python3
"""Run old memfd backend CPU/memfd pub/sub latency comparisons."""

import argparse
import csv
import os
import random
import subprocess
import sys
import time


RESULT_FIELDS = [
    "variant",
    "repeat",
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
BENCHMARK_FIELDS = RESULT_FIELDS[2:]


def command(role, mode, size, count, rate, warmup, affinity):
    command = [
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
        "--warmup",
        str(warmup),
    ]
    if affinity:
        command = ["taskset", "-c", affinity] + command
    return command


def parse_result(output, communication, mode, size):
    lines = [line for line in output.splitlines() if line.startswith("RESULT,")]
    if not lines:
        raise RuntimeError(
            f"no result: {communication}, {mode}, {size}; process output: {output}"
        )

    fields = next(csv.reader([lines[-1]]))
    if len(fields) != len(BENCHMARK_FIELDS) + 2 or fields[0] != "RESULT" or fields[1] != "ok":
        raise RuntimeError(f"benchmark error: {lines[-1]}")

    result = dict(zip(BENCHMARK_FIELDS, fields[2:]))
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


def run_inter_process_case(
    mode, size, count, rate, warmup, env, publisher_affinity, subscriber_affinity,
    discovery_wait
):
    communication = "inter_process"
    subscriber = subprocess.Popen(
        command("sub", mode, size, count, rate, warmup, subscriber_affinity),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    publisher = None
    try:
        time.sleep(discovery_wait)
        publisher = subprocess.Popen(
            command("pub", mode, size, count, rate, warmup, publisher_affinity),
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


def run_intra_process_case(mode, size, count, rate, warmup, env, affinity):
    communication = "intra_process_va"
    process = subprocess.Popen(
        command("intra", mode, size, count, rate, warmup, affinity),
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
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--seed", type=int, default=20260812)
    parser.add_argument("--variant", default="unspecified")
    parser.add_argument("--communications", default="inter_process,intra_process_va")
    parser.add_argument("--modes", default="cpu,memfd")
    parser.add_argument("--publisher-affinity")
    parser.add_argument("--subscriber-affinity")
    parser.add_argument("--intra-affinity")
    parser.add_argument("--discovery-wait", type=float, default=3.0)
    parser.add_argument("--timing-dir")
    args = parser.parse_args()

    if args.count <= args.warmup:
        parser.error("--count must be greater than --warmup")
    if args.rate_hz <= 0:
        parser.error("--rate-hz must be positive")
    if args.warmup < 0 or args.repeats <= 0:
        parser.error("--warmup must be non-negative and --repeats must be positive")

    env = os.environ.copy()
    env["RMW_IMPLEMENTATION"] = "rmw_fastrtps_cpp"
    sizes = [int(value) for value in args.sizes.split(",") if value]
    communications = [value for value in args.communications.split(",") if value]
    modes = [value for value in args.modes.split(",") if value]
    cases = [
        (communication, mode, size)
        for size in sizes
        for mode in modes
        for communication in communications
    ]
    rows = []
    for repeat in range(1, args.repeats + 1):
        repeat_cases = list(cases)
        random.Random(args.seed + repeat - 1).shuffle(repeat_cases)
        for communication, mode, size in repeat_cases:
            print(
                f"running repeat={repeat} {communication} {mode} {size} B",
                file=sys.stderr,
                flush=True,
            )
            case_env = env.copy()
            if args.timing_dir:
                os.makedirs(args.timing_dir, exist_ok=True)
                timing_path = os.path.join(
                    args.timing_dir,
                    f"{args.variant}-repeat{repeat}-{communication}-{mode}-{size}.csv",
                )
                try:
                    os.remove(timing_path)
                except FileNotFoundError:
                    pass
                case_env["RMW_FASTRTPS_PUBLISH_TIMING_FILE"] = timing_path
                case_env["RMW_FASTRTPS_PUBLISH_TIMING_VARIANT"] = args.variant
            if communication == "inter_process":
                result = run_inter_process_case(
                    mode, size, args.count, args.rate_hz, args.warmup, case_env,
                    args.publisher_affinity, args.subscriber_affinity,
                    args.discovery_wait,
                )
            else:
                result = run_intra_process_case(
                    mode, size, args.count, args.rate_hz, args.warmup, case_env,
                    args.intra_affinity,
                )
            if communication == "intra_process_va" and int(result["va_matches"]) != args.count:
                raise RuntimeError(f"intra-process VA sharing failed: {result}")
            result["variant"] = args.variant
            result["repeat"] = repeat
            rows.append(result)

    with open(args.output, "w", newline="", encoding="utf-8") as output_file:
        writer = csv.DictWriter(output_file, fieldnames=RESULT_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
