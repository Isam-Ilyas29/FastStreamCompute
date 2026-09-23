#include "faststreamcompute/bytecode_executor.hpp"
#include "faststreamcompute/program.hpp"
#include "faststreamcompute/record.hpp"
#include "faststreamcompute/reference_executor.hpp"

#include <cstddef>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

faststreamcompute::Program makeMidpointProgram() {
    faststreamcompute::Program program;
    const auto bid = program.inputf64("bid");
    const auto ask = program.inputf64("ask");
    const auto sum = program.add(bid, ask);
    program.emit("midpoint", program.mul(sum, program.constant(0.5)));
    return program;
}

bool checkBatch(faststreamcompute::BytecodeExecutor& executor,
                const faststreamcompute::Program& program,
                const std::vector<faststreamcompute::QuoteRecord>& records,
                const std::vector<double>& expected) {
    // NaN makes a missing output write fail even when the expected answer is zero.
    std::vector<double> output(records.size(),
                               std::numeric_limits<double>::quiet_NaN());
    faststreamcompute::batchExecute(executor, records, output);

    if (output.size() != expected.size()) {
        std::cerr << "Batch output count differs from expected count\n";
        return false;
    }
    for (std::size_t i = 0; i < records.size(); ++i) {
        const double reference =
            faststreamcompute::referenceExecutor(records[i], program).at("midpoint");
        if (output[i] != expected[i] || output[i] != reference) {
            std::cerr << "Batch mismatch at row " << i << ": got " << output[i]
                      << ", expected " << expected[i]
                      << ", reference " << reference << '\n';
            return false;
        }
    }
    return true;
}

bool checkSuccessfulBatches() {
    const auto program = makeMidpointProgram();
    faststreamcompute::ExecutionBlueprint blueprint(program);
    faststreamcompute::BytecodeExecutor executor(blueprint);

    // All calls reuse this same executor. Expected answers are hand calculated.
    if (!checkBatch(executor, program,
                    {{100.0, 102.0}, {200.0, 204.0}, {-3.0, 2.0}, {5.0, 5.0}},
                    {101.0, 202.0, -0.5, 5.0})) {
        return false;
    }
    // Change every input and keep the batch length: stale previous answers fail.
    if (!checkBatch(executor, program,
                    {{10.0, 14.0}, {-10.0, 2.0}, {0.0, 0.0}, {102.0, 100.0}},
                    {12.0, -4.0, 0.0, 101.0})) {
        return false;
    }
    if (!checkBatch(executor, program, {}, {})) {
        return false;
    }
    // A single row after an empty batch must still work.
    return checkBatch(executor, program, {{1.0, 2.0}}, {1.5});
}

bool expectInvalidArgument(faststreamcompute::BytecodeExecutor& executor,
                           std::span<const faststreamcompute::QuoteRecord> records,
                           std::vector<double>& output,
                           const char* description) {
    const auto original_output = output;
    try {
        faststreamcompute::batchExecute(executor, records, output);
    }
    catch (const std::invalid_argument&) {
        // The adapter should reject invalid arguments before writing any results.
        if (output != original_output) {
            std::cerr << description << ": output changed before rejection\n";
            return false;
        }
        return true;
    }
    catch (...) {
        std::cerr << description << ": wrong exception type\n";
        return false;
    }
    std::cerr << description << ": expected std::invalid_argument\n";
    return false;
}

bool checkInvalidArguments() {
    const auto program = makeMidpointProgram();
    faststreamcompute::ExecutionBlueprint blueprint(program);
    faststreamcompute::BytecodeExecutor executor(blueprint);
    const std::vector<faststreamcompute::QuoteRecord> records = {
        {100.0, 102.0}, {200.0, 204.0}};
    std::vector<double> short_output(1, -999.0);
    std::vector<double> long_output(3, -999.0);
    if (!expectInvalidArgument(executor, records, short_output, "Short output") ||
        !expectInvalidArgument(executor, records, long_output, "Long output")) {
        return false;
    }

    const faststreamcompute::Program no_outputs;
    faststreamcompute::ExecutionBlueprint empty_blueprint(no_outputs);
    faststreamcompute::BytecodeExecutor empty_executor(empty_blueprint);
    std::vector<double> output(records.size(), -999.0);
    if (!expectInvalidArgument(empty_executor, records, output, "Zero outputs")) {
        return false;
    }

    faststreamcompute::Program two_outputs;
    const auto bid = two_outputs.inputf64("bid");
    const auto ask = two_outputs.inputf64("ask");
    two_outputs.emit("bid", bid);
    two_outputs.emit("ask", ask);
    faststreamcompute::ExecutionBlueprint two_blueprint(two_outputs);
    faststreamcompute::BytecodeExecutor two_executor(two_blueprint);
    if (!expectInvalidArgument(two_executor, records, output, "Two outputs")) {
        return false;
    }

    // An empty batch does not waive the adapter's one-output requirement.
    std::vector<double> empty_output;
    if (!expectInvalidArgument(empty_executor, {}, empty_output,
                               "Empty batch with zero outputs") ||
        !expectInvalidArgument(two_executor, {}, empty_output,
                               "Empty batch with two outputs")) {
        return false;
    }
    // Rejected calls must not prevent later valid use of the executor.
    return checkBatch(executor, program, records, {101.0, 202.0});
}

} // namespace

int main() {
    if (!checkSuccessfulBatches() || !checkInvalidArguments()) {
        return 1;
    }
    return 0;
}
