#include "faststreamcompute/bytecode_executor.hpp"
#include "faststreamcompute/native_kernel.hpp"
#include "faststreamcompute/program.hpp"
#include "faststreamcompute/record.hpp"
#include "faststreamcompute/reference_executor.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

bool expect_values(const std::vector<double>& actual,
                   const std::vector<double>& expected,
                   const std::string& workload) {
    if (actual.size() != expected.size()) {
        std::cerr << workload << " produced " << actual.size()
                  << " values, expected " << expected.size() << '\n';
        return false;
    }

    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::cerr << workload << " mismatch at row " << i
                      << ": got " << actual[i]
                      << ", expected " << expected[i] << '\n';
            return false;
        }
    }

    return true;
}

template <typename Function>
bool expect_size_mismatch(Function function, const std::string& workload) {
    try {
        function();
    }
    catch (const std::invalid_argument&) {
        return true;
    }
    catch (...) {
        std::cerr << workload << " mismatch threw the wrong exception type\n";
        return false;
    }

    std::cerr << workload << " accepted mismatched input and output sizes\n";
    return false;
}

bool check_direct_results(const std::vector<faststreamcompute::QuoteRecord>& records,
                          const std::vector<double>& expected_midpoint,
                          const std::vector<double>& expected_spread,
                          const std::vector<double>& expected_relative_spread) {
    std::vector<double> midpoint(records.size());
    std::vector<double> spread(records.size());
    std::vector<double> relative_spread(records.size());

    faststreamcompute::midpointAOS(records, midpoint);
    faststreamcompute::spreadAOS(records, spread);
    faststreamcompute::relativeSpreadAOS(records, relative_spread);

    return expect_values(midpoint, expected_midpoint, "midpoint") &&
           expect_values(spread, expected_spread, "spread") &&
           expect_values(relative_spread, expected_relative_spread,
                         "relative spread");
}

bool check_empty_batches() {
    std::vector<faststreamcompute::QuoteRecord> records;
    std::vector<double> output;

    faststreamcompute::midpointAOS(records, output);
    faststreamcompute::spreadAOS(records, output);
    faststreamcompute::relativeSpreadAOS(records, output);

    return output.empty();
}

bool check_single_row() {
    const std::vector<faststreamcompute::QuoteRecord> records = {
        {100.0, 102.0},
    };
    const std::vector<double> expected_midpoint = {101.0};
    const std::vector<double> expected_spread = {2.0};
    const std::vector<double> expected_relative_spread = {2.0 / 202.0};

    return check_direct_results(records, expected_midpoint, expected_spread,
                                expected_relative_spread);
}

bool check_size_mismatches() {
    const std::vector<faststreamcompute::QuoteRecord> records = {{100.0, 102.0}};
    std::vector<double> short_output;
    std::vector<double> long_output(2);

    return expect_size_mismatch(
               [&] { faststreamcompute::midpointAOS(records, short_output); },
               "midpoint with short output") &&
           expect_size_mismatch(
               [&] { faststreamcompute::midpointAOS(records, long_output); },
               "midpoint with long output") &&
           expect_size_mismatch(
               [&] { faststreamcompute::spreadAOS(records, short_output); },
               "spread with short output") &&
           expect_size_mismatch(
               [&] { faststreamcompute::spreadAOS(records, long_output); },
               "spread with long output") &&
           expect_size_mismatch(
               [&] { faststreamcompute::relativeSpreadAOS(records, short_output); },
               "relative spread with short output") &&
           expect_size_mismatch(
               [&] { faststreamcompute::relativeSpreadAOS(records, long_output); },
               "relative spread with long output");
}

bool check_relative_spread_edges() {
    if (!std::numeric_limits<double>::is_iec559 || sizeof(double) != 8) {
        std::cerr << "Tests require 64-bit IEC 60559 double behavior\n";
        return false;
    }

    const std::vector<faststreamcompute::QuoteRecord> records = {
        {1.0, -1.0},
        {0.0, 0.0},
    };
    std::vector<double> output(records.size());
    faststreamcompute::relativeSpreadAOS(records, output);

    if (!std::isinf(output[0]) || !std::signbit(output[0])) {
        std::cerr << "nonzero divided by zero should produce negative infinity\n";
        return false;
    }
    if (!std::isnan(output[1])) {
        std::cerr << "zero divided by zero should produce NaN\n";
        return false;
    }

    return true;
}

faststreamcompute::Program make_midpoint_program() {
    faststreamcompute::Program program;
    const auto bid = program.inputf64("bid");
    const auto ask = program.inputf64("ask");
    const auto sum = program.add(bid, ask);
    const auto midpoint = program.mul(sum, program.constant(0.5));
    program.emit("midpoint", midpoint);
    return program;
}

faststreamcompute::Program make_spread_program() {
    faststreamcompute::Program program;
    const auto bid = program.inputf64("bid");
    const auto ask = program.inputf64("ask");
    program.emit("spread", program.sub(ask, bid));
    return program;
}

faststreamcompute::Program make_relative_spread_program() {
    faststreamcompute::Program program;
    const auto bid = program.inputf64("bid");
    const auto ask = program.inputf64("ask");
    const auto sum = program.add(bid, ask);
    const auto spread = program.sub(ask, bid);
    program.emit("relative_spread", program.div(spread, sum));
    return program;
}

bool check_backend(const std::vector<faststreamcompute::QuoteRecord>& records,
                   const std::vector<double>& native_output,
                   const std::vector<double>& expected,
                   const faststreamcompute::Program& program,
                   const std::string& output_name) {
    faststreamcompute::ExecutionBlueprint blueprint(program);
    faststreamcompute::BytecodeExecutor bytecode(blueprint);

    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto reference =
            faststreamcompute::referenceExecutor(records[i], program);
        const auto found = reference.find(output_name);
        if (found == reference.end() || found->second != expected[i]) {
            std::cerr << output_name << " reference mismatch at row " << i << '\n';
            return false;
        }

        const auto& bytecode_output = bytecode.execute(records[i]);
        if (bytecode_output.size() != 1 || bytecode_output[0] != expected[i]) {
            std::cerr << output_name << " bytecode mismatch at row " << i << '\n';
            return false;
        }

        if (native_output[i] != expected[i]) {
            std::cerr << output_name << " native mismatch at row " << i << '\n';
            return false;
        }
    }

    return true;
}

bool check_differential_results(
    const std::vector<faststreamcompute::QuoteRecord>& records,
    const std::vector<double>& expected_midpoint,
    const std::vector<double>& expected_spread,
    const std::vector<double>& expected_relative_spread) {
    std::vector<double> midpoint(records.size());
    std::vector<double> spread(records.size());
    std::vector<double> relative_spread(records.size());
    faststreamcompute::midpointAOS(records, midpoint);
    faststreamcompute::spreadAOS(records, spread);
    faststreamcompute::relativeSpreadAOS(records, relative_spread);

    const auto midpoint_program = make_midpoint_program();
    const auto spread_program = make_spread_program();
    const auto relative_spread_program = make_relative_spread_program();

    return check_backend(records, midpoint, expected_midpoint,
                         midpoint_program, "midpoint") &&
           check_backend(records, spread, expected_spread,
                         spread_program, "spread") &&
           check_backend(records, relative_spread, expected_relative_spread,
                         relative_spread_program, "relative_spread");
}

}  // namespace

int main() {
    const std::vector<faststreamcompute::QuoteRecord> records = {
        {100.0, 102.0},
        {200.0, 204.0},
        {102.0, 100.0},
        {-3.0, 2.0},
        {5.0, 5.0},
    };
    const std::vector<double> expected_midpoint = {
        101.0, 202.0, 101.0, -0.5, 5.0,
    };
    const std::vector<double> expected_spread = {
        2.0, 4.0, -2.0, 5.0, 0.0,
    };
    const std::vector<double> expected_relative_spread = {
        2.0 / 202.0,
        4.0 / 404.0,
        -2.0 / 202.0,
        -5.0,
        0.0,
    };

    if (!check_direct_results(records, expected_midpoint, expected_spread,
                              expected_relative_spread) ||
        !check_empty_batches() ||
        !check_single_row() ||
        !check_size_mismatches() ||
        !check_relative_spread_edges() ||
        !check_differential_results(records, expected_midpoint, expected_spread,
                                    expected_relative_spread)) {
        return 1;
    }

    return 0;
}
