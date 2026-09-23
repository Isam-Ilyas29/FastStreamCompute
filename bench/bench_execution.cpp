#include <benchmark/benchmark.h>

#include "faststreamcompute/bytecode_executor.hpp"
#include "faststreamcompute/native_kernel.hpp"
#include "faststreamcompute/program.hpp"
#include "faststreamcompute/record_generator.hpp"
#include "faststreamcompute/reference_executor.hpp"

#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>


namespace {
    // Builder API helpers

    faststreamcompute::Program makeMidpointProgram() {
        faststreamcompute::Program p;
        const auto bid = p.inputf64("bid");
        const auto ask = p.inputf64("ask");
        const auto sum = p.add(bid, ask);
        p.emit("midpoint", p.mul(sum, p.constant(0.5)));
        return p;
    }

    faststreamcompute::Program makeSpreadProgram() {
        faststreamcompute::Program p;
        const auto bid = p.inputf64("bid");
        const auto ask = p.inputf64("ask");
        p.emit("spread", p.sub(ask, bid));
        return p;
    }

    faststreamcompute::Program makeRelativeSpreadProgram() {
        faststreamcompute::Program p;
        const auto bid = p.inputf64("bid");
        const auto ask = p.inputf64("ask");
        const auto sum = p.add(bid, ask);
        const auto spread = p.sub(ask, bid);
        p.emit("relative_spread", p.div(spread, sum));
        return p;
    }

    // Correctness check helpers
    std::vector<double> makeExpected(std::span<const faststreamcompute::QuoteRecord> records, const faststreamcompute::Program& program, const std::string& output_name) {
        std::vector<double> expected;
        expected.reserve(records.size());

        for (const auto& record : records) {
            expected.push_back(faststreamcompute::referenceExecutor(record, program).at(output_name));
        }

        return expected;
    }

    bool checkOutput(benchmark::State& state, std::span<const double> output, std::span<const double> expected, std::string_view phase) {
        if (output.size() != expected.size()) {
            state.SkipWithError(std::format("{}: output size mismatch", phase));
            return false;
        }

        for (std::size_t i = 0; i < output.size(); ++i) {
            // Exact comparison is intentional for these finite inputs and the
            // matching operation order under our strict floating-point build flags.
            if (output[i] != expected[i]) {
                state.SkipWithError(std::format("{}: row {}: got {}, expected {}", phase, i, output[i], expected[i]));
                return false;
            }
        }
        return true;
    }
}

// Native
static void BM_MidpointNative(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto records = faststreamcompute::generateRecords(count);
    std::vector<double> output(count);
    const auto program = makeMidpointProgram();
    const auto expected = makeExpected(records, program, "midpoint");

    // Warmup
    for (unsigned int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::midpointAOS(records, output);
    }
    
    // Correctness check
    if (!checkOutput(state, output, expected, "before timing")) {
        return;
    }

    auto* output_data = output.data();
    benchmark::DoNotOptimize(output_data);

    for (auto _ : state) {
        faststreamcompute::midpointAOS(records, output);
        benchmark::ClobberMemory();
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "after timing")) {
        return;
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_SpreadNative(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto records = faststreamcompute::generateRecords(count);
    std::vector<double> output(count);
    const auto program = makeSpreadProgram();
    const auto expected = makeExpected(records, program, "spread");

    // Warmup
    for (unsigned int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::spreadAOS(records, output);
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "before timing")) {
        return;
    }

    auto* output_data = output.data();
    benchmark::DoNotOptimize(output_data);

    for (auto _ : state) {
        faststreamcompute::spreadAOS(records, output);
        benchmark::ClobberMemory();
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "after timing")) {
        return;
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_RelativeSpreadNative(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto records = faststreamcompute::generateRecords(count);
    std::vector<double> output(count);
    const auto program = makeRelativeSpreadProgram();
    const auto expected = makeExpected(records, program, "relative_spread");

    // Warmup
    for (unsigned int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::relativeSpreadAOS(records, output);
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "before timing")) {
        return;
    }

    auto* output_data = output.data();
    benchmark::DoNotOptimize(output_data);

    for (auto _ : state) {
        faststreamcompute::relativeSpreadAOS(records, output);
        benchmark::ClobberMemory();
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "after timing")) {
        return;
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

// Warm bytecode execution: records, expected values and storage are prepared before timing
static void BM_MidpointBytecodeExecute(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto records = faststreamcompute::generateRecords(count);
    std::vector<double> output(count);
    const auto program = makeMidpointProgram();
    const auto expected = makeExpected(records, program, "midpoint");

    faststreamcompute::ExecutionBlueprint blueprint(program);
    faststreamcompute::BytecodeExecutor executor(blueprint);

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::batchExecute(executor, records, output);
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "before timing")) {
        return;
    }

    auto* output_data = output.data();
    benchmark::DoNotOptimize(output_data);

    for (auto _ : state) {
        faststreamcompute::batchExecute(executor, records, output);
        benchmark::ClobberMemory();
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "after timing")) {
        return;
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_SpreadBytecodeExecute(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto records = faststreamcompute::generateRecords(count);
    std::vector<double> output(count);
    const auto program = makeSpreadProgram();
    const auto expected = makeExpected(records, program, "spread");

    faststreamcompute::ExecutionBlueprint blueprint(program);
    faststreamcompute::BytecodeExecutor executor(blueprint);

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::batchExecute(executor, records, output);
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "before timing")) {
        return;
    }

    auto* output_data = output.data();
    benchmark::DoNotOptimize(output_data);

    for (auto _ : state) {
        faststreamcompute::batchExecute(executor, records, output);
        benchmark::ClobberMemory();
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "after timing")) {
        return;
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_RelativeSpreadBytecodeExecute(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto records = faststreamcompute::generateRecords(count);
    std::vector<double> output(count);
    const auto program = makeRelativeSpreadProgram();
    const auto expected = makeExpected(records, program, "relative_spread");

    faststreamcompute::ExecutionBlueprint blueprint(program);
    faststreamcompute::BytecodeExecutor executor(blueprint);

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::batchExecute(executor, records, output);
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "before timing")) {
        return;
    }

    auto* output_data = output.data();
    benchmark::DoNotOptimize(output_data);

    for (auto _ : state) {
        faststreamcompute::batchExecute(executor, records, output);
        benchmark::ClobberMemory();
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "after timing")) {
        return;
    }
    
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

// Preparation: build the Program once and time blueprint/executor construction and destruction
static void BM_MidpointBytecodeCreateDestroy(benchmark::State& state) {
    const auto program = makeMidpointProgram();
    
    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
    }

    for (auto _ : state) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
        // Both local objects are destroyed here, inside the timed iteration.
    }
}

static void BM_SpreadBytecodeCreateDestroy(benchmark::State& state) {
    const auto program = makeSpreadProgram();

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
    }
    
    for (auto _ : state) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
        // Both local objects are destroyed here, inside the timed iteration.
    }
}

static void BM_RelativeSpreadBytecodeCreateDestroy(benchmark::State& state) {
    const auto program = makeRelativeSpreadProgram();

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
    }
    
    for (auto _ : state) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
        // Both local objects are destroyed here, inside the timed iteration.
    }
}

// Fresh-executor lifecycle: prepare, execute one batch, then destroy the objects.
static void BM_MidpointBytecodeCreateExecuteDestroy(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto records = faststreamcompute::generateRecords(count);
    std::vector<double> output(count);
    const auto program = makeMidpointProgram();
    const auto expected = makeExpected(records, program, "midpoint");

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        faststreamcompute::batchExecute(executor, records, output);
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "before timing")) {
        return;
    }

    auto* output_data = output.data();
    benchmark::DoNotOptimize(output_data);

    for (auto _ : state) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        faststreamcompute::batchExecute(executor, records, output);
        benchmark::ClobberMemory();
        // Destruction is included in this lifecycle measurement.
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "after timing")) {
        return;
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_SpreadBytecodeCreateExecuteDestroy(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto records = faststreamcompute::generateRecords(count);
    std::vector<double> output(count);
    const auto program = makeSpreadProgram();
    const auto expected = makeExpected(records, program, "spread");

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        faststreamcompute::batchExecute(executor, records, output);
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "before timing")) {
        return;
    }

    auto* output_data = output.data();
    benchmark::DoNotOptimize(output_data);

    for (auto _ : state) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        faststreamcompute::batchExecute(executor, records, output);
        benchmark::ClobberMemory();
        // Destruction is included in this lifecycle measurement.
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "after timing")) {
        return;
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_RelativeSpreadBytecodeCreateExecuteDestroy(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto records = faststreamcompute::generateRecords(count);
    std::vector<double> output(count);
    const auto program = makeRelativeSpreadProgram();
    const auto expected = makeExpected(records, program, "relative_spread");

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        faststreamcompute::batchExecute(executor, records, output);
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "before timing")) {
        return;
    }

    auto* output_data = output.data();
    benchmark::DoNotOptimize(output_data);
    
    for (auto _ : state) {
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        faststreamcompute::batchExecute(executor, records, output);
        benchmark::ClobberMemory();
        // Destruction is included in this lifecycle measurement.
    }

    // Correctness check
    if (!checkOutput(state, output, expected, "after timing")) {
        return;
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_MidpointNative)->Arg(4096)->Arg(65536)->Arg(1048576)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_MidpointBytecodeExecute)->Arg(4096)->Arg(65536)->Arg(1048576)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_MidpointBytecodeCreateDestroy)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_MidpointBytecodeCreateExecuteDestroy)->Arg(4096)->Arg(65536)->Arg(1048576)->UseRealTime()->Unit(benchmark::kNanosecond);

BENCHMARK(BM_SpreadNative)->Arg(4096)->Arg(65536)->Arg(1048576)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_SpreadBytecodeExecute)->Arg(4096)->Arg(65536)->Arg(1048576)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_SpreadBytecodeCreateDestroy)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_SpreadBytecodeCreateExecuteDestroy)->Arg(4096)->Arg(65536)->Arg(1048576)->UseRealTime()->Unit(benchmark::kNanosecond);

BENCHMARK(BM_RelativeSpreadNative)->Arg(4096)->Arg(65536)->Arg(1048576)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_RelativeSpreadBytecodeExecute)->Arg(4096)->Arg(65536)->Arg(1048576)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_RelativeSpreadBytecodeCreateDestroy)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_RelativeSpreadBytecodeCreateExecuteDestroy)->Arg(4096)->Arg(65536)->Arg(1048576)->UseRealTime()->Unit(benchmark::kNanosecond);
