/*
Investigate the overhead cost between chrono and TSC
*/

#include "faststreamcompute/bytecode_executor.hpp"
#include "faststreamcompute/native_kernel.hpp"
#include "faststreamcompute/record_generator.hpp"
#include "faststreamcompute/reference_executor.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <random>
#include <sched.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <x86intrin.h>
#include <cpuid.h>


namespace {
    // pointer escaping and memory barrier
    inline void escape(const void* pointer) {
        asm volatile("" : : "g"(pointer) : "memory");
    }
    inline void clobber() {
        asm volatile("" : : : "memory");
    }

    // TSC helpers
    inline std::uint64_t tscStart() {
        clobber();
        _mm_lfence();

        const std::uint64_t ticks = __rdtsc();

        _mm_lfence();
        clobber();

        return ticks;
    }
    inline std::uint64_t tscFinish() {
        return tscStart();
    }

    struct ChronoMeasurement {
        std::chrono::steady_clock::duration elapsed{};
        std::vector<std::chrono::steady_clock::duration> samples;
    };

    struct TscMeasurement {
        std::chrono::steady_clock::duration elapsed{};
        std::vector<std::uint64_t> samples; // Raw TSC ticks
    };

    // Limited to Intel processor
    double getTscFrequency() {
        double frequency = 0;
        // Nominal core crystal clock is found in cpuid.15 leaf according to Intel's manual
        // eax[31:0] denominator, ebx[31:0] numerator, ecx[31:0] nominal_art_frequency
        // Therefore frequency = ecx * (ebx / eax)
        std::uint32_t eax, ebx, ecx, edx;
        if (__get_cpuid(0x15, &eax, &ebx, &ecx, &edx) && eax != 0 && ebx != 0 && ecx != 0) {
            frequency = static_cast<double>(ecx) * (static_cast<double>(ebx) / static_cast<double>(eax));
        }
        
        return frequency;
    }

    std::size_t getSampleCount(std::size_t executions, unsigned int percent) {
        if (percent > 100) {
            throw std::invalid_argument("sample percentage must be between 0 and 100");
        }
        return static_cast<std::size_t>((executions / 100.0) * percent);

    }

    faststreamcompute::Program createMidpointProgram() {
        faststreamcompute::Program p;
        const auto bid = p.inputf64("bid");
        const auto ask = p.inputf64("ask");
        const auto sum = p.add(bid, ask);
        p.emit("midpoint", p.mul(sum, p.constant(0.5)));
        return p;
    }

    faststreamcompute::Program createSpreadProgram() {
        faststreamcompute::Program p;
        const auto bid = p.inputf64("bid");
        const auto ask = p.inputf64("ask");
        p.emit("spread", p.sub(ask, bid));
        return p;
    }

    std::vector<double> createExpected(std::span<const faststreamcompute::QuoteRecord> records, const faststreamcompute::Program& program, const std::string& output_name = "midpoint") {
        std::vector<double> expected;
        expected.reserve(records.size());
        for (const auto& record : records) {
            expected.push_back(faststreamcompute::referenceExecutor(record, program).at(output_name));
        }
        return expected;
    }

    bool validateOutputs(std::span<const double> output, std::span<const double> expected, std::string_view phase) {
        if (output.size() != expected.size()) {
            std::cerr << std::format("{}: output size mismatch\n", phase);
            return false;
        }
        for (std::size_t i = 0; i < output.size(); ++i) {
            if (output[i] != expected[i]) {
                std::cerr << std::format("{}: row {}: got {}, expected {}\n", phase, i, output[i], expected[i]);
                return false;
            }
        }
        return true;
    }

    template <typename Execute>
    ChronoMeasurement collectSamplesChrono(Execute execute, std::span<double> output, std::size_t execution_count, unsigned int sample_percent, unsigned int seed = 1234) {
        ChronoMeasurement result;
        const std::size_t sample_count = getSampleCount(execution_count, sample_percent);
        result.samples.resize(sample_count);

        // Choose sampled executions with a repeatable seed
        std::vector<unsigned char> selected;
        selected.resize(execution_count, 0);
        std::fill_n(selected.begin(), sample_count, 1);
        std::mt19937 generator(seed);
        std::shuffle(selected.begin(), selected.end(), generator);

        auto* output_data = output.data();
        auto* sample_data = result.samples.data();
        escape(output_data);
        escape(sample_data);

        // Warmup
        const auto warmup_end = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() < warmup_end) {
            execute();
            clobber();
        }

        std::size_t sample_index = 0;
        clobber();
        const auto outer_start = std::chrono::steady_clock::now();
        if (sample_percent == 0) {
            // None mode
            for (std::size_t i = 0; i < execution_count; ++i) {
                execute();
                clobber();
            }
        }
        else if (sample_percent < 100) {
            // Sparse mode
            for (std::size_t i = 0; i < execution_count; ++i) {
                if (selected[i]) {
                    const auto start = std::chrono::steady_clock::now();
                    execute();
                    clobber();
                    const auto finish = std::chrono::steady_clock::now();
                    result.samples[sample_index] = finish - start;
                    ++sample_index;
                }
                else {
                    execute();
                    clobber();
                }
            }
        }
        else {
            // Full mode
            for (std::size_t i = 0; i < execution_count; ++i) {
                const auto start = std::chrono::steady_clock::now();
                execute();
                clobber();
                const auto finish = std::chrono::steady_clock::now();
                result.samples[i] = finish - start;
            }
        }
        clobber();
        const auto outer_finish = std::chrono::steady_clock::now();
        result.elapsed = outer_finish - outer_start;

        return result;
    }

    template <typename Execute>
    TscMeasurement collectSamplesTsc(Execute execute, std::span<double> output, std::size_t execution_count, unsigned int sample_percent, unsigned int seed = 1234) {
        TscMeasurement result;
        const std::size_t sample_count = getSampleCount(execution_count, sample_percent);
        result.samples.resize(sample_count);

        // Choose sampled executions with a repeatable seed
        std::vector<unsigned char> selected;
        selected.resize(execution_count, 0);
        std::fill_n(selected.begin(), sample_count, 1);
        std::mt19937 generator(seed);
        std::shuffle(selected.begin(), selected.end(), generator);

        auto* output_data = output.data();
        auto* sample_data = result.samples.data();
        escape(output_data);
        escape(sample_data);

        // Warmup 
        const auto warmup_end = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() < warmup_end) {
            execute();
            clobber();
        }

        std::size_t sample_index = 0;
        clobber();
        const auto outer_start = std::chrono::steady_clock::now();
        if (sample_percent == 0) {
            // None mode
            for (std::size_t i = 0; i < execution_count; ++i) {
                execute();
                clobber();
            }
        }
        else if (sample_percent < 100) {
            // Sparse mode
            for (std::size_t i = 0; i < execution_count; ++i) {
                if (selected[i]) {
                    const auto start = tscStart();
                    execute();
                    const auto finish = tscFinish();
                    result.samples[sample_index] = finish - start;
                    ++sample_index;
                }
                else {
                    execute();
                    clobber();
                }
            }
        }
        else {
            // Full mode
            for (std::size_t i = 0; i < execution_count; ++i) {
                const auto start = tscStart();
                execute();
                const auto finish = tscFinish();
                result.samples[i] = finish - start;
            }
        }
        clobber();
        const auto outer_finish = std::chrono::steady_clock::now();
        result.elapsed = outer_finish - outer_start;

        return result;
    }

    std::vector<std::chrono::steady_clock::duration> findChronoOverhead(std::size_t sample_count) {
        std::vector<std::chrono::steady_clock::duration> samples(sample_count);
        escape(samples.data());

        // Warmup
        const auto warmup_end = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() < warmup_end) {
            const auto start = std::chrono::steady_clock::now();
            clobber();
            const auto finish = std::chrono::steady_clock::now();
        }

        for (std::size_t i = 0; i < sample_count; ++i) {
            const auto start = std::chrono::steady_clock::now();
            clobber();
            const auto finish = std::chrono::steady_clock::now();

            samples[i] = finish - start;
        }
        clobber();

        return samples;
    }

    std::vector<std::uint64_t> findTscOverhead(std::size_t sample_count) {
        std::vector<std::uint64_t> samples(sample_count);
        escape(samples.data());

        // Warmup
        const auto warmup_end = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() < warmup_end) {
            tscStart();
            tscFinish();
        }

        for (std::size_t i = 0; i < sample_count; ++i) {
            const auto start = tscStart();
            const auto finish = tscFinish();
            samples[i] = finish - start;
        }
        clobber();
        return samples;
    }

    double ticksToNs(std::uint64_t ticks, double hz) {
        return static_cast<double>(ticks) * 1'000'000'000.0 / hz;
    }

    // Nearest-rank percentiles: sort a copy first; never reorder raw samples.
    double getPercentile(std::span<const double> sorted, double percent) {
        if (sorted.empty() || !std::isfinite(percent) || percent <= 0 || percent > 100) {
            throw std::invalid_argument("percentile requires samples and a percentage greater than 0 and at most 100");
        }
        const std::size_t rank = static_cast<std::size_t>(std::ceil(sorted.size() * percent / 100.0));
        return sorted[std::clamp(rank, std::size_t{1}, sorted.size()) - 1];
    }

    struct Summary {
        double loop_ms = 0;
        std::size_t sample_count = 0;
        double p50 = 0;
        double p99 = 0;
        double p99_9 = 0;
        double maximum = 0;
    };

    void printResult(std::string_view name, const Summary& result, const Summary& baseline, std::size_t executions) {
        const double extra_ns = (result.loop_ms - baseline.loop_ms) * 1'000'000 / executions;
        const double change = (result.loop_ms / baseline.loop_ms - 1) * 100;
        std::cout << std::format("{} | samples {} | loop / ms {:.3f} | extra / ns per execution {:+.3f} | change / % {:+.3f}\n",
            name, result.sample_count, result.loop_ms, extra_ns, change);
        if (result.sample_count > 0) {
            std::cout << std::format("p50 / ns {:.3f} | p99 / ns {:.3f} | p99.9 / ns {:.3f} | max / ns {:.3f}\n",
                result.p50, result.p99, result.p99_9, result.maximum);
        }
        else {
            std::cout << "No individual timestamps: no latency percentiles for None.\n";
        }
    }
}

int main() {
    constexpr std::size_t executions = 10'000'000;
    constexpr std::size_t overhead_samples = 1'000'000;
    constexpr unsigned int rounds = 5;
    constexpr unsigned int sparse_percent = 1;

    const double frequency = getTscFrequency();
    if (frequency <= 0) {
        std::cerr << "TSC frequency is unavailable\n";
        return 1;
    }

    const auto records = faststreamcompute::generateRecords(256);
    const auto program = createMidpointProgram();
    const auto expected = createExpected(records, program);
    faststreamcompute::ExecutionBlueprint blueprint(program);
    faststreamcompute::ChunkedBytecodeExecutor executor(blueprint, 256);
    std::vector<double> output(records.size());
    const auto chunked = [&]() { executor.execute(records, output); };
    const auto native = [&]() { faststreamcompute::midpointAOS(records, output); };

    chunked();
    if (!validateOutputs(output, expected, "chunked before timing")) {
        return 1;
    }
    std::fill(output.begin(), output.end(), 0.0);
    native();
    if (!validateOutputs(output, expected, "native before timing")) {
        return 1;
    }

    auto chrono_overhead = findChronoOverhead(overhead_samples);
    std::vector<double> chrono_overhead_ns;
    chrono_overhead_ns.reserve(chrono_overhead.size());
    for (const auto duration : chrono_overhead) {
        chrono_overhead_ns.push_back(std::chrono::duration<double, std::nano>(duration).count());
    }
    std::sort(chrono_overhead_ns.begin(), chrono_overhead_ns.end());

    const auto tsc_overhead = findTscOverhead(overhead_samples);
    std::vector<double> tsc_overhead_ns;
    tsc_overhead_ns.reserve(tsc_overhead.size());
    for (const auto ticks : tsc_overhead) {
        tsc_overhead_ns.push_back(ticksToNs(ticks, frequency));
    }
    std::sort(tsc_overhead_ns.begin(), tsc_overhead_ns.end());

    std::cout << std::format("Chrono empty boundary: p50 {:.3f} ns, p99 {:.3f} ns, p99.9 {:.3f} ns\n",
        getPercentile(chrono_overhead_ns, 50.0), getPercentile(chrono_overhead_ns, 99.0),
        getPercentile(chrono_overhead_ns, 99.9));
    std::cout << std::format("TSC empty boundary: p50 {:.3f} ns, p99 {:.3f} ns, p99.9 {:.3f} ns\n",
        getPercentile(tsc_overhead_ns, 50.0), getPercentile(tsc_overhead_ns, 99.0),
        getPercentile(tsc_overhead_ns, 99.9));

    const std::string_view names[] = {"None", "Chrono Sparse", "Chrono Full", "TSC Sparse", "TSC Full"};
    const unsigned int percentages[] = {0, sparse_percent, 100, sparse_percent, 100};
    const std::string_view backends[] = {"Chunked", "Native"};
    Summary results[2][rounds][5];

    std::cout << std::format("\n256-record midpoint batch, {} executions, {} rounds, TSC {:.0f} Hz\n",
        executions, rounds, frequency);

    for (unsigned int round = 0; round < rounds; ++round) {
        for (unsigned int position = 0; position < 5; ++position) {
            const unsigned int mode = (round + position) % 5;
            // Alternate which backend runs first in each pair.
            for (unsigned int order = 0; order < 2; ++order) {
                const unsigned int backend = (round + position + order) % 2;
                std::fill(output.begin(), output.end(), 0.0);
                std::vector<double> samples;
                std::chrono::steady_clock::duration elapsed;

                if (mode < 3) {
                    const auto measurement = backend == 0
                        ? collectSamplesChrono(chunked, output, executions, percentages[mode], 1234 + round)
                        : collectSamplesChrono(native, output, executions, percentages[mode], 1234 + round);
                    elapsed = measurement.elapsed;
                    samples.reserve(measurement.samples.size());
                    for (const auto duration : measurement.samples) {
                        samples.push_back(std::chrono::duration<double, std::nano>(duration).count());
                    }
                }
                else {
                    const auto measurement = backend == 0
                        ? collectSamplesTsc(chunked, output, executions, percentages[mode], 1234 + round)
                        : collectSamplesTsc(native, output, executions, percentages[mode], 1234 + round);
                    elapsed = measurement.elapsed;
                    samples.reserve(measurement.samples.size());
                    for (const auto ticks : measurement.samples) {
                        samples.push_back(ticksToNs(ticks, frequency));
                    }
                }

                auto& result = results[backend][round][mode];
                result.loop_ms = std::chrono::duration<double, std::milli>(elapsed).count();
                result.sample_count = samples.size();
                if (!samples.empty()) {
                    std::sort(samples.begin(), samples.end());
                    result.p50 = getPercentile(samples, 50.0);
                    result.p99 = getPercentile(samples, 99.0);
                    result.p99_9 = getPercentile(samples, 99.9);
                    result.maximum = samples.back();
                }

                if (!validateOutputs(output, expected, backends[backend])) {
                    return 1;
                }
            }
        }
        std::cout << std::format("Completed round {}\n", round + 1) << std::flush;
    }

    for (unsigned int round = 0; round < rounds; ++round) {
        std::cout << std::format("\nRound {}\n", round + 1);
        for (unsigned int backend = 0; backend < 2; ++backend) {
            std::cout << backends[backend] << '\n';
            for (unsigned int mode = 0; mode < 5; ++mode) {
                printResult(names[mode], results[backend][round][mode], results[backend][round][0], executions);
            }
        }
        std::cout << std::format("None: chunked {:.3f} ns/batch, native {:.3f} ns/batch; chunked/native {:.2f}x\n",
            results[0][round][0].loop_ms * 1'000'000 / executions,
            results[1][round][0].loop_ms * 1'000'000 / executions,
            results[0][round][0].loop_ms / results[1][round][0].loop_ms);
    }

    std::cout << "\nPercentiles include timer overhead. No samples were corrected or removed.\n";
    std::cout << "Each measured execution processes one prepared batch of 256 records.\n";
    return 0;
}
