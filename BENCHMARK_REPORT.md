# memfd_buffer_backend pub/sub benchmark report

This report is for `src/memfd_buffer_backend` only.

## Measurement definition

The benchmark compares a normal CPU `rosidl::Buffer<uint8_t>` with the old
`memfd_buffer_backend` implementation in two modes:

- `inter_process`: publisher and subscriber are separate processes.
- `intra_process_va`: publisher and subscriber are in one process with
  `use_intra_process_comms(true)`, `SharedPtr` buffers, and `unique_ptr`
  publication.

The start timestamp is written immediately before `publish()` using
`std::chrono::steady_clock`.  The subscriber stops the timer immediately after
loading `data()[0]`.  Allocation and payload writing are outside the timed
interval.  Intra-process rows verify publisher/subscriber buffer address
equality.

Each case uses `rmw_fastrtps_cpp`, Reliable `KeepLast(10)`, 10 Hz, and 30
messages.  The first 10 callbacks are warm-up; p50/p95/p99 use the remaining
20 samples.

The Lyrical source tree was not rebuilt.  The existing installed Lyrical build
was used.  There is a known issue under investigation in the current
`rmw_fastrtps_cpp` implementation, so inter-process values should be treated
as provisional until that separate issue is fixed and the benchmark is rerun.

## Results

The raw result file is generated as
`memfd-old-pubsub-results.csv` by the command in the README.  The result table
was collected on 2026-08-12.  Values are microseconds; each cell is
`p50 / p95 / p99`.  All rows received 30 messages and used 20 post-warm-up
samples.  `va_matches` was 0 for inter-process rows and 30 for every
intra-process row.

| size (bytes) | inter CPU | inter memfd | intra CPU | intra memfd |
|---:|---:|---:|---:|---:|
| 64 | 770.056 / 850.547 / 850.547 | 1035.930 / 1065.240 / 1065.240 | 57.639 / 77.890 / 77.890 | 75.668 / 138.820 / 138.820 |
| 1,024 | 697.704 / 812.389 / 812.389 | 850.497 / 1180.990 / 1180.990 | 102.165 / 133.015 / 133.015 | 65.796 / 90.566 / 90.566 |
| 4,096 | 592.829 / 781.916 / 781.916 | 991.855 / 1126.210 / 1126.210 | 73.111 / 88.906 / 88.906 | 74.436 / 90.553 / 90.553 |
| 16,384 | 740.505 / 888.191 / 888.191 | 1064.020 / 1139.820 / 1139.820 | 73.724 / 86.132 / 86.132 | 76.994 / 91.943 / 91.943 |
| 65,536 | 773.666 / 903.516 / 903.516 | 935.654 / 1086.790 / 1086.790 | 67.277 / 90.614 / 90.614 | 69.962 / 82.013 / 82.013 |
| 262,144 | 894.638 / 1100.350 / 1100.350 | 889.876 / 1071.660 / 1071.660 | 79.057 / 85.005 / 85.005 | 76.958 / 92.952 / 92.952 |
| 1,048,576 | 13493.600 / 13761.600 / 13761.600 | 961.243 / 1226.570 / 1226.570 | 88.182 / 98.136 / 98.136 | 72.589 / 99.720 / 99.720 |
| 4,194,304 | 14598.900 / 15334.500 / 15334.500 | 926.827 / 1102.660 / 1102.660 | 76.242 / 93.587 / 93.587 | 75.442 / 114.358 / 114.358 |
| 16,777,216 | 29909.600 / 30743.300 / 30743.300 | 743.883 / 880.376 / 880.376 | 57.395 / 72.076 / 72.076 | 86.108 / 102.161 / 102.161 |

The dominant observation is that the inter-process CPU path grows sharply for
1 MiB and larger payloads, while the memfd path stays near 0.7–1.0 ms in this
run.  The intra-process VA-sharing paths stay roughly below 0.14 ms across
the sweep, because the subscriber reads the same published buffer address.
These values are a single run and are not a substitute for rerunning after the
known `rmw_fastrtps_cpp` issue is fixed.
