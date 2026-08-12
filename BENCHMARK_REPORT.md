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

The initial size sweep used the existing installed Lyrical build.  The
`rmw_fastrtps_cpp` patch comparison below rebuilt only `rmw_fastrtps_cpp` from
the Lyrical source tree.  There is a known issue under investigation in the
current `rmw_fastrtps_cpp` implementation, so inter-process values should be
treated as provisional until that separate issue is fixed.

## Results: initial patched run

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

## `rmw_fastrtps_cpp` patch comparison

This comparison measures the effect of the patch in
`~/ros2_lyrical/src/ros2/rmw_fastrtps/rmw_fastrtps_cpp/src/rmw_publish.cpp`.
The patch changes `publish_to_buffer_endpoints()` as follows:

- Before the patch, each endpoint called `get_serialized_size()`, allocated a
  `std::vector<uint8_t>` of that size, zero-initialized it, and passed it to an
  externally backed `FastBuffer`.
- With the patch, one default `FastBuffer` is created outside the endpoint loop
  and reused by each CDR serializer.  Its storage grows lazily without the
  upfront zero-initialization.

The baseline was measured with the patch stashed, followed by a rebuild of
`rmw_fastrtps_cpp`.  The patch was then restored and rebuilt, and the same
36-case sweep was repeated.  Both runs used the same benchmark conditions as
above; every row received 30 messages, measured 20 samples after warm-up, and
all intra-process rows had `va_matches=30`.

The table below shows the relevant `inter_process/memfd` path.  Values are
microseconds; the delta is the patch result relative to baseline.

| size (bytes) | baseline p50 / p95 | patched p50 / p95 | p50 delta |
|---:|---:|---:|---:|
| 64 | 1016.290 / 1322.760 | 981.530 / 1122.840 | -3.4% |
| 1,024 | 431.551 / 517.723 | 1026.350 / 1087.680 | +137.8% |
| 4,096 | 383.556 / 491.389 | 810.187 / 967.861 | +111.2% |
| 16,384 | 809.654 / 1081.910 | 882.337 / 990.808 | +9.0% |
| 65,536 | 275.783 / 394.435 | 911.630 / 1049.730 | +230.6% |
| 262,144 | 643.886 / 1868.780 | 803.279 / 1055.220 | +24.8% |
| 1,048,576 | 395.543 / 650.925 | 1014.560 / 1141.300 | +156.5% |
| 4,194,304 | 588.854 / 638.210 | 893.714 / 1036.960 | +51.8% |
| 16,777,216 | 1322.980 / 1506.730 | 613.110 / 728.274 | -53.7% |

The patch improved the 16 MiB case by about 54%, but regressed most smaller
and medium-sized cases.  Across the nine sizes, the geometric mean of
`patched / baseline` p50 was 1.50x, and the patch was faster in only 2 of 9
sizes.  The likely trade-off is that lazy growth avoids the large zero-fill
cost for very large payloads, while repeated growth and allocation costs more
for smaller payloads.

In this benchmark there is one non-CPU subscriber endpoint.  Also,
`FastBuffer` is local to each `rmw_publish()` call, so its capacity is not
reused across messages; the patch's endpoint-loop reuse does not amortize
allocation across the 30 published samples.  The CPU inter-process path has
no non-CPU endpoint, and intra-process communication does not go through this
RMW publish path.  Therefore their run-to-run differences are treated as
measurement variability rather than a direct patch effect.

Raw files:

- `memfd-old-pubsub-results-baseline.csv`: patch-before baseline.
- `memfd-old-pubsub-results-patched-rerun.csv`: same-condition patched rerun.
- `memfd-old-pubsub-results.csv`: initial patched run.

## `FastBuffer::reserve()` patch comparison

The lazy-allocation patch was then changed to call
`FastBuffer::reserve(get_serialized_size(ros_message) + 4)` before creating the
CDR serializer.  This uses the same capacity as the original
`std::vector<uint8_t>(serialized_size + 4)` allocation, but `FastBuffer::reserve`
uses `malloc()` and does not value-initialize the allocation.  The buffer is
still created once per `rmw_publish()` call and reused across endpoints.

The reserve version compiled successfully and passed a smoke test before the
same 36-case sweep.  All rows again received 30 messages, measured 20 samples,
and had `va_matches=30` for intra-process communication.

The relevant `inter_process/memfd` p50/p95 results are below.  The deltas are
relative to the patch-before baseline; the lazy patch result is included for
comparison.

| size (bytes) | baseline p50 / p95 | lazy patch p50 | reserve patch p50 / p95 | reserve vs baseline |
|---:|---:|---:|---:|---:|
| 64 | 1016.290 / 1322.760 | 981.530 | 945.648 / 1211.090 | -7.0% |
| 1,024 | 431.551 / 517.723 | 1026.350 | 893.716 / 1054.880 | +107.1% |
| 4,096 | 383.556 / 491.389 | 810.187 | 939.673 / 1106.600 | +145.0% |
| 16,384 | 809.654 / 1081.910 | 882.337 | 776.918 / 1159.470 | -4.0% |
| 65,536 | 275.783 / 394.435 | 911.630 | 914.328 / 1120.340 | +231.5% |
| 262,144 | 643.886 / 1868.780 | 803.279 | 990.277 / 1127.600 | +53.8% |
| 1,048,576 | 395.543 / 650.925 | 1014.560 | 840.327 / 1089.160 | +112.4% |
| 4,194,304 | 588.854 / 638.210 | 893.714 | 922.418 / 1103.820 | +56.6% |
| 16,777,216 | 1322.980 / 1506.730 | 613.110 | 675.496 / 756.288 | -48.9% |

In this run, the reserve patch was faster than the baseline at 3 of 9 sizes
and its geometric-mean p50 ratio was 1.50x baseline.  Compared with the lazy
patch, the reserve version had an almost identical geometric-mean p50
(`reserve/lazy = 1.00x`) and was faster at 4 of 9 sizes.  Therefore, reserving
the old allocation size did not restore the small and medium-size performance
to the baseline level.  It removes the dynamic growth path, but it does not
remove the allocation itself, and the measured latency is also affected by
the known `rmw_fastrtps_cpp` inter-process behavior and run-to-run variation.

The reserve result file is `memfd-old-pubsub-results-reserve.csv`.

## patch の影響分析と今後の提案（日本語）

### 結論

今回の patch は、大きな payload で発生する zero-initialization を避ける
方向には働いています。しかし、`FastBuffer` は `rmw_publish()` ごとに作り
直されるため、publish 間で allocation は再利用されません。そのため、
小〜中サイズで baseline 相当まで戻る効果は確認できませんでした。

`inter_process/memfd` の p50 では、16 MiB が baseline の 1323 µs から
reserve 版の 675 µs へ約49%改善しました。一方、1 KiB は 432 µs から
894 µs、64 KiB は 276 µs から 914 µs、1 MiB は 396 µs から 840 µs でした。

ただし、各条件は1回の実行で、warm-up後20サンプルだけを使っています。
計測区間は publisher の `publish()` 直前から subscriber の読み出しまで
なので、RMW内部だけでなく DDS送信、プロセススケジューリング、subscriber
側の処理も含みます。小〜中サイズの差を patch の回帰と断定するには、まだ
追加計測が必要です。

### 何が起きているか

- baseline は `get_serialized_size()`、`std::vector<uint8_t>` の allocation、
  zero-initialization、外部 buffer を使った CDR serialize を行います。
- lazy 版は `get_serialized_size()` と事前 zero-initialization を省きますが、
  CDR serialize 中に `FastBuffer` の動的拡張が発生し得ます。
- reserve 版は `serialized_size + 4` を `FastBuffer::reserve()` します。
  これにより動的拡張を避けますが、publish ごとの `malloc()` と
  `get_serialized_size()` は残ります。
- 今回、reserve/lazy の p50 幾何平均は約 `1.00x` でした。したがって、
  `FastBuffer::resize()` だけが主なボトルネックとは考えにくい結果です。
- benchmark では非CPU subscriber endpoint が1つだけです。そのため、
  endpoint間で `FastBuffer` を再利用する patch の利点も十分に評価できて
  いません。実際には buffer は message ごとに作り直されています。

### rmw_fastrtps_cpp への提案

次の提案としては、単に `reserve()` を追加するより、publisher lifetime
で buffer capacity を再利用する設計を検討するのが有効です。

1. `FastBuffer` を `rmw_publish()` のローカル変数ではなく publisher state
   に保持し、複数 message 間で capacity を再利用する。
2. `rmw_publish()` の並行呼び出しに対応するため、mutex、thread-local
   buffer、または buffer pool のいずれかを使う。
3. `write_w_timestamp()` が return 前に CDR buffer を完全にコピーすることを
   確認し、buffer reuse が安全であることを明示する。
4. endpoint間再利用を評価するため、複数の memfd subscriber endpoint を
   用いた benchmark を追加する。

rmw_fastrtps_cpp には、「patch で一律に遅くなった」と伝えるより、次のよう
に伝えるのが適切です。

> 大容量では zero-initialization 回避の効果が確認できる。一方、
> per-publish allocation が残るため、小〜中サイズの改善は確認できない。
> 次の検証として publisher lifetime の buffer reuse と、serialize/write
> 区間の内部計測が必要である。

### 追加で有用なエビデンス

`publish_to_buffer_endpoints()` を次の区間に分けて計測すると、原因を直接
切り分けられます。

```text
get_serialized_size()
FastBuffer allocation / reserve / resize
cdr_serialize_with_endpoint()
write_w_timestamp()
```

各ケースで、少なくとも次の値を記録するのが有効です。

- `serialized_size`
- reserve または resize 後の `FastBuffer` capacity
- serialize 後の実データ長
- `resize()` 回数
- allocation 回数と確保 byte 数
- 各区間の所要時間
- endpoint 数
- publisher 側の `publish()` 呼び出し時間

比較対象は、baseline、lazy、reserve、publisher lifetime で buffer を再利用
する版の4種類にします。各条件を複数回実行し、CPU affinity を固定した
median と信頼区間を比較すると、今回のような DDS と scheduler の揺らぎを
分離しやすくなります。`perf stat` の cycles、cache-misses、minor-faults
も、zero-fill と allocator の影響を裏付ける証拠になります。

## `unique_ptr<uint8_t[]>` による zero-initialize 除去の計測

最後に、元の baseline の構造を維持し、buffer の型だけを
`std::vector<uint8_t>` から `std::unique_ptr<uint8_t[]>` に変更した variant
を計測しました。各 endpoint ごとに次の allocation を行っています。

```cpp
std::unique_ptr<uint8_t[]> buffer_data(new uint8_t[buffer_size]);
```

明示的な初期化子を付けない `new[]` を使用し、zero-initialize を避けて
います。`std::make_unique<uint8_t[]>` は実装上の初期化規則を混同しやすい
ため、この計測では使用していません。`FastBuffer` は従来と同じ外部 buffer
を参照し、allocation のタイミングと endpoint ごとの構造も baseline と
同じです。

36条件を再計測した結果、全行で `received=30`、`measured=20`、
intra-process の `va_matches=30` でした。以下は relevant な
`inter_process/memfd` の p50/p95 です。単位は µs です。

| size (bytes) | baseline p50 / p95 | unique_ptr p50 / p95 | p50 delta |
|---:|---:|---:|---:|
| 64 | 1016.290 / 1322.760 | 775.092 / 880.679 | -23.7% |
| 1,024 | 431.551 / 517.723 | 397.996 / 460.516 | -7.8% |
| 4,096 | 383.556 / 491.389 | 354.722 / 441.143 | -7.5% |
| 16,384 | 809.654 / 1081.910 | 527.157 / 749.331 | -34.9% |
| 65,536 | 275.783 / 394.435 | 370.824 / 485.868 | +34.5% |
| 262,144 | 643.886 / 1868.780 | 418.750 / 588.119 | -35.0% |
| 1,048,576 | 395.543 / 650.925 | 433.941 / 585.454 | +9.7% |
| 4,194,304 | 588.854 / 638.210 | 411.010 / 537.198 | -30.2% |
| 16,777,216 | 1322.980 / 1506.730 | 419.081 / 640.957 | -68.3% |

unique_ptr/baseline の p50 幾何平均は `0.765x` で、9サイズ中7サイズで
unique_ptr 版が高速でした。lazy 版および reserve 版と比較しても、p50 の
幾何平均はそれぞれ `0.509x` でした。

この結果は、今回の変更では `FastBuffer` の dynamic growth や
publisher lifetime の再利用を導入していないため、baseline との差分を
主に zero-initialize の有無に帰属しやすい点が重要です。16 MiB だけでなく
1 KiB〜4 KiBでも baseline より良くなっており、reserve 版で観測した
小〜中サイズの揺らぎとは異なる傾向です。ただし、各条件1 runの結果で
あるため、最終判断には複数run、CPU affinity固定、publisher内部区間の
直接計測が必要です。

この variant は、`rmw_fastrtps_cpp` に対して最初に提案しやすい変更です。
従来の endpoint ごとの buffer lifetime と外部 `FastBuffer` を保ったまま、
不要な zero-fill だけを除去しているため、serialize の動作や buffer の
所有権モデルを変更せずに効果を検証できます。これを確認した上で、次の
段階として publisher lifetime の buffer reuse を検討するのが安全です。

生データは `memfd-old-pubsub-results-unique-ptr.csv` です。
