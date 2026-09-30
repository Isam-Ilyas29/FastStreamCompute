#include <benchmark/benchmark.h>

#include "faststreamcompute/bytecode_executor.hpp"
#include "faststreamcompute/program_optimiser.hpp"
#include "faststreamcompute/record_generator.hpp"
#include "faststreamcompute/reference_executor.hpp"

#include <cstddef>
#include <format>
#include <span>
#include <string_view>
#include <vector>


namespace {
    constexpr std::size_t chunk_capacity = 256;

    faststreamcompute::Program createOptimisationProgram() {
        faststreamcompute::Program p;
        p.constant(99.0);
        const auto bid = p.inputf64("bid");
        const auto ask = p.inputf64("ask");
        const auto two = p.constant(2.0);
        const auto three = p.constant(3.0);
        const auto six = p.mul(two, three);
        const auto sum = p.add(bid, ask);
        p.emit("sum", sum);
        const auto duplicate = p.add(bid, ask);
        p.emit("weighted", p.mul(duplicate, six));
        p.mul(ask, two);
        return p;
    }

    std::vector<std::vector<double>> createExpectedOutputs(std::span<const faststreamcompute::QuoteRecord> records, const faststreamcompute::Program& program) {
        std::vector<std::vector<double>> expected(2, std::vector<double>(records.size())); // 2 outputs

        for (std::size_t row = 0; row < records.size(); ++row) {
            const auto values = faststreamcompute::referenceExecutor(records[row], program);
            expected[0][row] = values.at("sum");
            expected[1][row] = values.at("weighted");
        }

        return expected;
    }

    bool validateOutputs(benchmark::State& state, std::span<const faststreamcompute::OutputBuffer> outputs, const std::vector<std::vector<double>>& expected, std::string_view phase) {
        for (std::size_t lane = 0; lane < outputs.size(); ++lane) {
            for (std::size_t row = 0; row < outputs[lane].size(); ++row) {
                if (outputs[lane][row] != expected[lane][row]) {
                    state.SkipWithError(std::format("{}: output {}, row {} differs from reference", phase, lane, row));
                    return false;
                }
            }
        }
        return true;
    }

    // Track metrics for program optimisation
    void setPlanCounters(benchmark::State& state, const faststreamcompute::Program& program, const faststreamcompute::ExecutionBlueprint& blueprint) {
        state.counters["nodes"] = static_cast<double>(program.getNodes().size());
        state.counters["instructions"] = static_cast<double>(blueprint.getInstructions().size());
        state.counters["registers"] = static_cast<double>(blueprint.getRegisterCount());
        state.counters["scratch_bytes"] = static_cast<double>(blueprint.getRegisterCount() * chunk_capacity * sizeof(double));
    }
}

static void BM_ProgramOptimisationExecute(benchmark::State& state, faststreamcompute::OptimisationOptions options) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto records = faststreamcompute::generateRecords(count);
    const auto original = createOptimisationProgram();
    const auto expected = createExpectedOutputs(records, original);
    const auto program = faststreamcompute::optimiseProgram(original, options);
    faststreamcompute::ExecutionBlueprint blueprint(program);
    faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);
    std::vector<double> sums(count), weighted(count);
    const faststreamcompute::OutputBuffer outputs[] = {sums, weighted};

    // Warmup
    for (unsigned int warmup = 0; warmup < 3; ++warmup) {
        executor.execute(records, outputs);
    }

    // Correctness check
    if (!validateOutputs(state, outputs, expected, "before timing")) {
        return;
    }

    auto* sum_data = sums.data();
    auto* weighted_data = weighted.data();
    benchmark::DoNotOptimize(sum_data);
    benchmark::DoNotOptimize(weighted_data);

    for (auto _ : state) {
        executor.execute(records, outputs);
        benchmark::ClobberMemory();
    }

    // Correctness check
    if (!validateOutputs(state, outputs, expected, "after timing")) {
        return;
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
    setPlanCounters(state, program, blueprint);
}

// Original builder work is outside timing. All scenarios use the same preparation API;
// None copies the original Program without transforming it. Destruction is included.
static void BM_ProgramOptimisationCreateDestroy(benchmark::State& state, faststreamcompute::OptimisationOptions options) {
    const auto original = createOptimisationProgram();
    const auto records = faststreamcompute::generateRecords(845);
    const auto expected = createExpectedOutputs(records, original);
    std::vector<double> sums(records.size()), weighted(records.size());
    const faststreamcompute::OutputBuffer outputs[] = {sums, weighted};

    // Warmup and correctness check
    for (unsigned int warmup = 0; warmup < 3; ++warmup) {
        const auto program = faststreamcompute::optimiseProgram(original, options);
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);
        executor.execute(records, outputs);
        if (!validateOutputs(state, outputs, expected, "preparation warmup")) {
            return;
        }
    }

    for (auto _ : state) {
        const auto program = faststreamcompute::optimiseProgram(original, options);
        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, chunk_capacity);
        benchmark::DoNotOptimize(executor);
        benchmark::ClobberMemory();
        // Program, blueprint and executor are destroyed inside timing.
    }
}

// Cases: no optimisations, one of each, all three
BENCHMARK_CAPTURE(BM_ProgramOptimisationExecute, None, (faststreamcompute::OptimisationOptions{false, false, false}))->Arg(4096)->Arg(65536)->UseRealTime();
BENCHMARK_CAPTURE(BM_ProgramOptimisationExecute, DeadNodes, (faststreamcompute::OptimisationOptions{true, false, false}))->Arg(4096)->Arg(65536)->UseRealTime();
BENCHMARK_CAPTURE(BM_ProgramOptimisationExecute, ConstantFolding, (faststreamcompute::OptimisationOptions{false, true, false}))->Arg(4096)->Arg(65536)->UseRealTime();
BENCHMARK_CAPTURE(BM_ProgramOptimisationExecute, CSE, (faststreamcompute::OptimisationOptions{false, false, true}))->Arg(4096)->Arg(65536)->UseRealTime();
BENCHMARK_CAPTURE(BM_ProgramOptimisationExecute, All, (faststreamcompute::OptimisationOptions{true, true, true}))->Arg(4096)->Arg(65536)->UseRealTime();

BENCHMARK_CAPTURE(BM_ProgramOptimisationCreateDestroy, None, (faststreamcompute::OptimisationOptions{false, false, false}))->UseRealTime();
BENCHMARK_CAPTURE(BM_ProgramOptimisationCreateDestroy, DeadNodes, (faststreamcompute::OptimisationOptions{true, false, false}))->UseRealTime();
BENCHMARK_CAPTURE(BM_ProgramOptimisationCreateDestroy, ConstantFolding, (faststreamcompute::OptimisationOptions{false, true, false}))->UseRealTime();
BENCHMARK_CAPTURE(BM_ProgramOptimisationCreateDestroy, CSE, (faststreamcompute::OptimisationOptions{false, false, true}))->UseRealTime();
BENCHMARK_CAPTURE(BM_ProgramOptimisationCreateDestroy, All, (faststreamcompute::OptimisationOptions{true, true, true}))->UseRealTime();
