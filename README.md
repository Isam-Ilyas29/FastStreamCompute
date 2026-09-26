# FastStreamCompute

FastStreamCompute is a C++ 20 engine that prepares a numerical computation once and runs it repeatedly over input records. The goal is to get a reusable engine closer to the speed of specialised native C++, while keeping it able to run different supported expressions without changing the executor.

## Current Hypothesis

**For Spread over 4,096 records, bytecode interpretation takes 16.9x longer, has 17.7x more instructions, executions 16.7x more instruction, but marginally has a 
higher IPC. perf record had majority of samples (80%)  inside BytecodeExecutor::execute, there I performed perf annotate to view the assembly. Initially,
I saw the native implementation using SIMD so investigated that. So, I used compiler flags "-fno-tree-loop-vectorize", "-fno-tree-slp-vectorize" and
remeasured. I found that that in this instance bytecode interpretation was only 12.2x slower. With vectorisation enabled, native executes: 28.5% fewer instructions 
and 12.8% fewer cycles. The faster version has lowe IPC so IPC alone does not mean a faster program. Surprisingly, native performance only improved by 1.17x and
bytecode performance also improved by 1.14x. However, I believe that the biggest improvement can come from bytecode executing less instructions since it has a higher IPC
than native and runs 17.7x more instructions than native. It calculates opcodes and operands every instruction for batchExecute even if it is the same, I should find a 
way to reduce this control logic. Contiguous arithmetic loops should strive to enable automatic vectorisation.**

## Benchmarks

### Linux perf

Spread on an i7-8700T running Manjaro: 4,096 records per batch, 100,000 iterations, five runs. [Raw results](docs/benchmarks/spread_ipc.txt).

| Measurement | Native | Bytecode |
|---|---:|---:|
| Median batch time / ns | 2,368 | 39,998 |
| Mean instructions per run / billions | 2.065 | 36.471 |
| Mean cycles per run / billions | 0.741 | 12.341 |
| Instructions per cycle | 2.79 | 2.96 |

The counters cover the whole process; batch times cover the benchmark loop. [perf report](docs/benchmarks/spread-bytecode-report.txt) puts 80.61% of cycle samples in `BytecodeExecutor::execute`. A [separate vectorisation check](docs/benchmarks/spread-vector-comparison.txt) found a 1.17x native speedup with vectorisation enabled, while bytecode remained 12.32x slower with it disabled in both builds. Next is measuring whether chunking reduces the repeated instruction handling.

### Windows baseline

These charts use [the 23 September 2026 Windows run](docs/benchmarks/bench_260923.json), with 20 repetitions per case. Lower times are better; each dot is a repetition average, not an individual record's latency. These are baseline measurements, not proof of performance on every workload.

### Midpoint

Bytecode execution stays near 5.4 ns per record across batch sizes.

![Midpoint performance](docs/benchmarks/graphs/bench_260923_Midpoint_performance.png)

Native midpoint results vary more at the largest batch size.

![Midpoint variability](docs/benchmarks/graphs/bench_260923_Midpoint_variability.png)

### Spread

Bytecode spread takes roughly 4.1 ns per record across batch sizes.

![Spread performance](docs/benchmarks/graphs/bench_260923_Spread_performance.png)

Bytecode spread results cluster fairly tightly across repetitions.

![Spread variability](docs/benchmarks/graphs/bench_260923_Spread_variability.png)

### Relative spread

Bytecode relative spread takes roughly 5.7–5.9 ns per record.

![Relative spread performance](docs/benchmarks/graphs/bench_260923_RelativeSpread_performance.png)

The largest batch includes an unusually slow bytecode execution repetition.

![Relative spread variability](docs/benchmarks/graphs/bench_260923_RelativeSpread_variability.png)

### Creation and destruction

Median creation and destruction costs range from about 295 to 409 ns.

![BytecodeCreateDestroy performance](docs/benchmarks/graphs/bench_260923_BytecodeCreateDestroy_performance.png)

All three creation benchmarks have occasional slower repetition averages.

![BytecodeCreateDestroy variability](docs/benchmarks/graphs/bench_260923_BytecodeCreateDestroy_variability.png)
