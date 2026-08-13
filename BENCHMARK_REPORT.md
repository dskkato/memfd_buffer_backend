# SHM Buffer Backend and `rmw_fastrtps_cpp` Patch Benchmark

## Executive summary

This report evaluates four independently built variants across all four requested
communication paths:

| Variant | `rmw_fastrtps_cpp` implementation |
|---|---|
| baseline | Unmodified `origin/lyrical` (`636a108`) |
| unique_ptr | Patch `0001-fastrtps-reuse-unique-ptr-buffer.patch` |
| lazy | Patch `0002-fastrtps-reuse-fastbuffer-lazy.patch` |
| reserve | Patch `0003-fastrtps-reuse-fastbuffer-reserve.patch` |

The three patches were applied independently to the same baseline; they are
alternative implementations, not cumulative changes.

The main conclusions are:

- Intra-process communication is the fastest path, at approximately 60–75 µs
  p50 in the baseline run. CPU-backed and SHM-backed intra-process results are
  close; SHM adds a small overhead in some sizes, but the overhead is often
  within end-to-end measurement variability.
- For inter-process communication, the CPU fallback becomes strongly
  payload-size dependent at 1 MiB and above. The baseline p50 reaches 12.5 ms
  at 1 MiB and 15.7 ms at 16 MiB.
- Baseline inter-process SHM is much faster than the CPU fallback for large
  payloads, but it still grows from 0.88 ms at 1 MiB to 2.52 ms at 16 MiB.
  Every RMW patch removes most of this large-payload growth; the patched SHM
  p50 at 16 MiB is 0.50–0.60 ms.
- The strongest and most consistent patch effect is on the inter-process SHM
  path. The geometric-mean p50 relative to baseline is 0.75x for `unique_ptr`,
  0.70x for `lazy`, and 0.75x for `reserve`.
- No communication-path regression can be attributed to the patches in this
  benchmark. The patched function is used by the non-CPU inter-process path;
  CPU inter-process and intra-process results are control paths and show normal
  run-to-run variation.

## Measurement design

### Matrix

Each variant was measured using the existing end-to-end benchmark with:

- 9 payload sizes: 64 B, 1 KiB, 4 KiB, 16 KiB, 64 KiB, 256 KiB, 1 MiB,
  4 MiB, and 16 MiB.
- 10 Hz publication rate.
- 30 messages per case, with the first 10 callbacks excluded as warm-up and
  20 samples used for the reported percentiles.
- 5 repeats per payload/path combination.
- Reliable `KeepLast(10)` QoS and `rmw_fastrtps_cpp`.
- Publisher CPU affinity 8, subscriber CPU affinity 9, and intra-process
  affinity 8.

The complete matrix is:

```text
4 variants × 9 sizes × 5 repeats ×
  (inter-process CPU, inter-process SHM,
   intra-process CPU, intra-process SHM)
= 720 cases
```

The benchmark timestamp is assigned immediately before `publish()`. The
subscriber records the end timestamp immediately after obtaining the selected
buffer view and loading its first byte. Allocation and payload initialization
are outside the benchmark timestamp interval. Intra-process cases additionally
verify that publisher and subscriber data addresses match for all 30 messages.

For each case, the CSV stores p50, p95, and p99. Tables in this report show the
median of each repeat's p50 or p95; the range of repeat p50 values is used when
discussing variability. All displayed latency values are microseconds unless
otherwise stated.

### Variant construction

The baseline was checked out at `origin/lyrical` commit `636a108`. Each patch
was then applied to that clean baseline and built in a separate Release
build/install prefix. After the measurements, the source checkout was restored
to the pre-existing local `lyrical` branch at `1aaf7d3`.

In this report, “SHM” refers to the benchmark's `memfd` backend. “CPU” refers
to the normal CPU-backed `rosidl::Buffer` fallback.

## Objective 1: benefits of the SHM buffer backend

### Baseline latency across all communication paths

The following table is the baseline reference. Each cell is `p50 / p95`.

| Payload | Inter-process CPU | Inter-process SHM | Intra-process CPU | Intra-process SHM |
|---:|---:|---:|---:|---:|
| 64 | 464.2 / 606.6 | 648.0 / 866.7 | 65.0 / 80.6 | 65.2 / 94.1 |
| 1,024 | 485.7 / 608.5 | 665.0 / 863.7 | 64.3 / 79.3 | 65.1 / 82.1 |
| 4,096 | 473.5 / 585.9 | 661.1 / 841.8 | 64.5 / 79.7 | 59.3 / 81.9 |
| 16,384 | 514.9 / 623.0 | 600.3 / 869.8 | 58.0 / 81.1 | 72.5 / 82.3 |
| 65,536 | 514.1 / 664.0 | 657.5 / 898.1 | 63.6 / 88.2 | 66.9 / 83.9 |
| 262,144 | 717.9 / 938.2 | 667.9 / 928.2 | 66.7 / 94.0 | 66.9 / 84.7 |
| 1,048,576 | 12,523.3 / 13,080.2 | 883.0 / 1,115.8 | 70.1 / 90.7 | 64.4 / 91.2 |
| 4,194,304 | 14,163.0 / 15,089.1 | 1,325.0 / 1,622.6 | 67.2 / 102.5 | 74.8 / 94.7 |
| 16,777,216 | 15,747.1 / 16,260.7 | 2,523.4 / 2,827.4 | 56.8 / 64.4 | 69.5 / 88.1 |

### Intra-process behavior

Intra-process communication is the fastest path throughout the size sweep. The
baseline p50 ranges are 56.8–70.1 µs for CPU and 59.3–74.8 µs for SHM. The
address check passed for all intra-process cases, confirming that the benchmark
used the intended direct intra-process buffer path.

The expectation that SHM introduces some intra-process overhead is supported at
some sizes: SHM is 25.1% slower than CPU at 16 KiB and 22.4% slower at 16 MiB.
However, it is not a uniform penalty: SHM is slightly faster at 4 KiB and 1
MiB in this run. Therefore, the defensible conclusion is that intra-process
communication is already dominated by a small fixed end-to-end cost, and any
additional SHM bookkeeping is small compared with scheduler and executor
variation for many sizes.

### Inter-process behavior and SHM benefit

For payloads through 256 KiB, the two inter-process backends are of the same
order of magnitude. At 1 MiB and above, the CPU fallback changes regime: its
p50 rises to 12.5–15.7 ms, while the baseline SHM path remains between 0.88 ms
and 2.52 ms. At 16 MiB, baseline SHM is 6.2x faster than baseline CPU by p50.

This establishes the practical SHM benefit for recurring, same-host large
buffers: the subscriber can consume the shared buffer without transferring the
full payload through the normal CPU serialization path.

## Objective 2: the large-buffer publish problem

### Observed symptom

The baseline inter-process SHM path is not perfectly constant with payload
size. Its p50 increases from 0.88 ms at 1 MiB to 1.33 ms at 4 MiB and 2.52 ms
at 16 MiB. This is the problem investigated by the RMW patches. The SHM pool
and subscriber-side direct access are not sufficient by themselves to produce
constant end-to-end latency when the publisher still prepares a size-dependent
temporary serialization buffer.

### Effect of the three RMW alternatives

The table shows inter-process SHM results for the large-buffer region. Each cell
is `p50 / p95`.

| Payload | Baseline | `unique_ptr` | `lazy` | `reserve` |
|---:|---:|---:|---:|---:|
| 1 MiB | 883.0 / 1,115.8 | 700.5 / 924.6 | 595.1 / 885.4 | 631.9 / 865.5 |
| 4 MiB | 1,325.0 / 1,622.6 | 622.7 / 889.5 | 602.0 / 858.7 | 645.9 / 846.2 |
| 16 MiB | 2,523.4 / 2,827.4 | 603.3 / 684.4 | 504.2 / 671.0 | 544.8 / 668.1 |

All three alternatives remove the dominant size-dependent component at large
payloads. The 16 MiB p50 reduction is 76.1% for `unique_ptr`, 80.0% for
`lazy`, and 78.4% for `reserve` relative to baseline. After the change, the
three variants are within roughly 0.5–0.7 ms at 1–16 MiB, which is the expected
near-constant SHM publish behavior much more closely.

The most direct explanation is the allocation behavior in
`publish_to_buffer_endpoints()`:

- Baseline allocates `std::vector<uint8_t>(buffer_size)`, which value-initializes
  the entire size-dependent temporary buffer.
- `unique_ptr` keeps the original external `FastBuffer` and per-endpoint
  lifetime but uses `new uint8_t[]` without an initializer.
- `lazy` uses an internally managed `FastBuffer` and lets it grow as needed.
- `reserve` uses an internally managed `FastBuffer` and explicitly reserves the
  serialized size before CDR serialization.

The end-to-end results identify avoidance of the baseline zero-fill/allocation
cost as the important factor. They do not prove that a publisher-lifetime
buffer pool is needed: none of these three patches reuses the temporary buffer
across publish calls.

## Objective 3: `rmw_fastrtps_cpp` fix and regression check

### Path-level comparison

The table gives the geometric-mean p50 ratio over the nine payload sizes,
variant divided by baseline. The `faster / within ±2% / slower` counts are
descriptive counts of the nine size points.

| Variant | Inter CPU | Inter SHM | Intra CPU | Intra SHM |
|---|---:|---:|---:|---:|
| `unique_ptr` | 0.917x; 6 / 3 / 0 | 0.750x; 6 / 0 / 3 | 0.922x; 6 / 2 / 1 | 0.903x; 7 / 0 / 2 |
| `lazy` | 0.946x; 4 / 4 / 1 | 0.701x; 6 / 2 / 1 | 0.977x; 5 / 2 / 2 | 0.957x; 5 / 1 / 3 |
| `reserve` | 0.981x; 3 / 4 / 2 | 0.752x; 5 / 2 / 2 | 0.896x; 8 / 1 / 0 | 0.942x; 4 / 5 / 0 |

The inter-process SHM column is the direct target path. All three alternatives
improve its large-payload behavior, and all have a geometric-mean p50 of about
0.70–0.75x baseline. Small-size changes are mixed: for example, `reserve` is
11.4% slower at 16 KiB, while `lazy` is 24.2% faster at 64 KiB. These isolated
points should not be treated as definitive regressions without more repetitions
or direct internal timing.

The other three columns are regression-control paths. Intra-process publication
does not execute the RMW buffer-endpoint publishing function, and the CPU
inter-process path has no non-CPU endpoint. Consequently, the apparent
improvements or regressions in those columns are end-to-end run variation, not
direct evidence that the patch changes those communication paths. No systematic
regression was observed in those controls.

### Patch-specific interpretation

- `unique_ptr` is the smallest conceptual change. It preserves the external
  `FastBuffer` and per-endpoint structure while removing value initialization.
  It produces a strong large-buffer improvement and a 0.75x geometric-mean
  p50 on the direct target path.
- `lazy` avoids both the external allocation and the explicit size calculation
  in the endpoint loop. It has the best direct-path geometric-mean p50 in this
  experiment, 0.70x baseline, and reaches 0.50 ms at 16 MiB.
- `reserve` avoids lazy growth while retaining an internally managed
  `FastBuffer`. Its direct-path result is similar to `unique_ptr`, but reserving
  does not eliminate all allocation work and does not provide a clear advantage
  over the lazy alternative in this end-to-end measurement.

The result supports accepting the zero-initialization fix as a focused RMW
change candidate. A separate publisher-lifetime buffer reuse design should be
evaluated independently, especially with concurrent publishes and multiple
non-CPU endpoints.

## Validation and reproducibility

### Benchmark validation

Each of the four raw CSV files contains 180 rows: 5 repeats × 4 paths × 9
payload sizes. All 720 cases across the four files satisfy the following:

- `received=30`.
- `measured=20`.
- Every intra-process row has `va_matches=30`.
- The reported backend matches the requested CPU or SHM mode.
- No benchmark timeout, backend mismatch, or intra-process address-sharing
  failure occurred.

Raw results:

- [baseline.csv](../../benchmark-results-16way/baseline.csv)
- [unique_ptr.csv](../../benchmark-results-16way/unique_ptr.csv)
- [lazy.csv](../../benchmark-results-16way/lazy.csv)
- [reserve.csv](../../benchmark-results-16way/reserve.csv)

### Build and test status

All four variants compiled successfully as Release builds in separate install
prefixes. The benchmark matrix itself completed successfully for every
variant.

The `rmw_fastrtps_cpp` package test target was also invoked for every build.
All four test runs were blocked by the same pre-existing environment problem:
the test runner imports `ament_cmake_test`, but the active Lyrical Python
environment does not provide that module. The failure occurred before the
individual tests ran (`ModuleNotFoundError: No module named ament_cmake_test`),
including for the unmodified baseline. Therefore, the
test result is inconclusive for patch regressions; it is not a patch-specific
failure.

### Limitations

- This is one host and one execution configuration. CPU affinity was fixed,
  but scheduler, DDS discovery, and executor effects remain in the end-to-end
  interval.
- Each case contains only 20 post-warm-up samples per repeat. The reported
  repeat median and range are more robust than a single run, but they are not a
  formal confidence interval.
- The benchmark uses one non-CPU endpoint, so it does not measure buffer reuse
  across several non-CPU endpoints.
- The result demonstrates the externally visible large-buffer effect. Direct
  timings for `get_serialized_size()`, allocation, CDR serialization, and
  `write_w_timestamp()` would provide stronger component-level attribution.

## Recommended follow-up

For upstream `rmw_fastrtps_cpp` review, the most defensible next step is to
submit or further validate the smallest zero-initialization change, then add
focused tests for:

1. Multiple non-CPU endpoints receiving the same published message.
2. Concurrent calls to `rmw_publish()`.
3. Serialization correctness for small descriptors and large SHM-backed
   payloads.
4. Internal allocation and serialization timing, separated from DDS delivery
   and subscriber scheduling.

The existing [`BENCHMARK_REPORT.md`](BENCHMARK_REPORT.md) is intentionally left
unchanged; this file is the reorganized objective-based report for the complete
16-way measurement.
