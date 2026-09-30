#include <benchmark/benchmark.h>

#include "faststreamcompute/bytecode_executor.hpp"
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

    faststreamcompute::Program createLongFusionProgram() {
        faststreamcompute::Program p;
        const auto bid = p.inputf64("bid");
        const auto ask = p.inputf64("ask");
        const auto half = p.constant(0.5);
        const auto two = p.constant(2.0);
        const auto three = p.constant(3.0);
        const auto quarter = p.constant(0.25);
        const auto seven = p.constant(7.0);

        const auto midpoint = p.mul(p.add(bid, ask), half);
        const auto scaled_spread = p.mul(p.sub(ask, bid), two);
        const auto adjusted_product = p.add(p.mul(bid, ask), three);
        const auto ratio = p.div(p.sub(adjusted_product, scaled_spread), midpoint);
        const auto scaled_combined = p.mul(p.add(ratio, adjusted_product), quarter);
        const auto second_ratio = p.div(p.sub(scaled_combined, scaled_spread), ask);
        const auto result = p.add(p.mul(second_ratio, midpoint), seven);
        p.emit("result", result);
        return p;
    }

    faststreamcompute::Program createMidpointProgram() {
        faststreamcompute::Program p;
        const auto bid = p.inputf64("bid");
        const auto ask = p.inputf64("ask");
        const auto sum = p.add(bid, ask);
        p.emit("midpoint", p.mul(sum, p.constant(0.5)));
        return p;
    }

    // Correctness check helpers
    std::vector<double> createExpected(std::span<const faststreamcompute::QuoteRecord> records, const faststreamcompute::Program& program, const std::string& output_name) {
        std::vector<double> expected;
        expected.reserve(records.size());

        for (const auto& record : records) {
            expected.push_back(faststreamcompute::referenceExecutor(record, program).at(output_name));
        }

        return expected;
    }

    bool validateOutputs(benchmark::State& state, std::span<const double> output, std::span<const double> expected, std::string_view phase) {
        if (output.size() != expected.size()) {
            state.SkipWithError(std::format("{}: output size mismatch", phase));
            return false;
        }

        for (std::size_t i = 0; i < output.size(); ++i) {
            if (output[i] != expected[i]) {
                state.SkipWithError(std::format("{}: row {}: got {}, expected {}", phase, i, output[i], expected[i]));
                return false;
            }
        }
        return true;
    }
}

// Warm chunked bytecode execution: records, expected values and storage are prepared before timing
static void BM_LongFusionChunkedBytecodeExecuteFusionOn(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto chunk_capacity = static_cast<std::size_t>(state.range(1));
    const auto records = faststreamcompute::generateRecords(count);
    std::vector<double> output(count);
    const auto program = createLongFusionProgram();
    const auto expected = createExpected(records, program, "result");

    faststreamcompute::ExecutionBlueprint blueprint(program, true);
    faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        executor.execute(records, output);
    }

    // Correctness check
    if (!validateOutputs(state, output, expected, "before timing")) {
        return;
    }

    auto* output_data = output.data();
    benchmark::DoNotOptimize(output_data);

    for (auto _ : state) {
        executor.execute(records, output);
        benchmark::ClobberMemory();
    }

    // Correctness check
    if (!validateOutputs(state, output, expected, "after timing")) {
        return;
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_LongFusionChunkedBytecodeExecuteFusionOff(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto chunk_capacity = static_cast<std::size_t>(state.range(1));
    const auto records = faststreamcompute::generateRecords(count);
    std::vector<double> output(count);
    const auto program = createLongFusionProgram();
    const auto expected = createExpected(records, program, "result");

    faststreamcompute::ExecutionBlueprint blueprint(program, false);
    faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        executor.execute(records, output);
    }

    // Correctness check
    if (!validateOutputs(state, output, expected, "before timing")) {
        return;
    }

    auto* output_data = output.data();
    benchmark::DoNotOptimize(output_data);

    for (auto _ : state) {
        executor.execute(records, output);
        benchmark::ClobberMemory();
    }

    // Correctness check
    if (!validateOutputs(state, output, expected, "after timing")) {
        return;
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

// Preparation: build the Program once and time blueprint/executor construction and destruction
static void BM_LongFusionChunkedBytecodeCreateDestroyFusionOn(benchmark::State& state) {
    const auto chunk_capacity = static_cast<std::size_t>(state.range(0));
    const auto program = createLongFusionProgram();

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::ExecutionBlueprint blueprint(program, true);
        faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
    }

    for (auto _ : state) {
        faststreamcompute::ExecutionBlueprint blueprint(program, true);
        faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
    }
}

static void BM_LongFusionChunkedBytecodeCreateDestroyFusionOff(benchmark::State& state) {
    const auto chunk_capacity = static_cast<std::size_t>(state.range(0));
    const auto program = createLongFusionProgram();

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::ExecutionBlueprint blueprint(program, false);
        faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
    }

    for (auto _ : state) {
        faststreamcompute::ExecutionBlueprint blueprint(program, false);
        faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
    }
}

static void BM_MidpointChunkedBytecodeCreateDestroyFusionOn(benchmark::State& state) {
    const auto chunk_capacity = static_cast<std::size_t>(state.range(0));
    const auto program = createMidpointProgram();

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::ExecutionBlueprint blueprint(program, true);
        faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
    }

    for (auto _ : state) {
        faststreamcompute::ExecutionBlueprint blueprint(program, true);
        faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
    }
}

static void BM_MidpointChunkedBytecodeCreateDestroyFusionOff(benchmark::State& state) {
    const auto chunk_capacity = static_cast<std::size_t>(state.range(0));
    const auto program = createMidpointProgram();

    // Warmup
    for (int warmup = 0; warmup < 3; ++warmup) {
        faststreamcompute::ExecutionBlueprint blueprint(program, false);
        faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
    }

    for (auto _ : state) {
        faststreamcompute::ExecutionBlueprint blueprint(program, false);
        faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
    }
}

BENCHMARK(BM_LongFusionChunkedBytecodeExecuteFusionOn)->Args({4096, 256})->Args({65536, 256})->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_LongFusionChunkedBytecodeExecuteFusionOff)->Args({4096, 256})->Args({65536, 256})->UseRealTime()->Unit(benchmark::kNanosecond);

BENCHMARK(BM_LongFusionChunkedBytecodeCreateDestroyFusionOn)->Arg(256)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_LongFusionChunkedBytecodeCreateDestroyFusionOff)->Arg(256)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_MidpointChunkedBytecodeCreateDestroyFusionOn)->Arg(256)->UseRealTime()->Unit(benchmark::kNanosecond);
BENCHMARK(BM_MidpointChunkedBytecodeCreateDestroyFusionOff)->Arg(256)->UseRealTime()->Unit(benchmark::kNanosecond);
