#include "faststreamcompute/bytecode_executor.hpp"
#include "faststreamcompute/reference_executor.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace faststreamcompute;

void require(bool condition, const std::string& message) {
    // Unlike assert(), this check also runs in Release builds.
    if (!condition) throw std::runtime_error(message);
}

enum class Workload { Spread, Midpoint, RelativeSpread, Constant };

Program makeProgram(Workload workload) {
    Program p;
    if (workload == Workload::Constant) {
        p.emit("result", p.constant(-2.5));
        return p;
    }
    const auto bid = p.inputf64("bid");
    const auto ask = p.inputf64("ask");
    if (workload == Workload::Spread) {
        p.emit("result", p.sub(ask, bid));
    } else if (workload == Workload::Midpoint) {
        p.emit("result", p.mul(p.add(bid, ask), p.constant(0.5)));
    } else {
        p.emit("result", p.div(p.sub(ask, bid), p.add(bid, ask)));
    }
    // An output need not be the final instruction. Later work must not overwrite it.
    p.add(bid, p.constant(123.0));
    return p;
}

double expectedValue(Workload workload, const QuoteRecord& record) {
    switch (workload) {
        case Workload::Spread: return record.ask - record.bid;
        case Workload::Midpoint: return (record.bid + record.ask) * 0.5;
        case Workload::RelativeSpread:
            return (record.ask - record.bid) / (record.bid + record.ask);
        case Workload::Constant: return -2.5;
    }
    throw std::logic_error("Unknown test workload");
}

void checkResults() {
    for (const auto workload : {Workload::Spread, Workload::Midpoint,
                               Workload::RelativeSpread, Workload::Constant}) {
        const auto program = makeProgram(workload);
        const ExecutionBlueprint blueprint(program);
        for (const std::size_t capacity : {1, 4, 7, 256}) {
            ChunkedBytecodeExecutor executor(blueprint, capacity);
            const std::vector<std::size_t> sizes = {
                0, 1, capacity - 1, capacity, capacity + 1,
                2 * capacity - 1, 2 * capacity, 2 * capacity + 1,
                845, 0, 1
            };
            // Reuse the same executor across lengths and changed input values.
            for (const auto size : sizes) {
                for (int pass = 0; pass < 2; ++pass) {
                    std::vector<QuoteRecord> records(size);
                    for (std::size_t row = 0; row < size; ++row) {
                        const double bid = 100.0 + static_cast<double>(row) + pass;
                        const double spread = (pass == 0 ? 1.0 : -1.0)
                            * (static_cast<double>(row) + 1.0);
                        records[row] = {bid, bid + spread};
                    }
                    const auto original_records = records;
                    // NaN exposes missing writes, even if an expected result is zero.
                    std::vector<double> output(size, std::numeric_limits<double>::quiet_NaN());
                    executor.execute(records, output);
                    for (std::size_t row = 0; row < size; ++row) {
                        const double reference = referenceExecutor(records[row], program).at("result");
                        const auto context = "workload=" + std::to_string(static_cast<int>(workload))
                            + " capacity=" + std::to_string(capacity)
                            + " size=" + std::to_string(size)
                            + " pass=" + std::to_string(pass)
                            + " row=" + std::to_string(row);
                        require(output[row] == expectedValue(workload, records[row])
                                && output[row] == reference, "Output mismatch: " + context);
                        require(records[row].bid == original_records[row].bid
                                && records[row].ask == original_records[row].ask,
                                "Input modified: " + context);
                    }
                }
            }
        }
    }
}

void checkFusion() {
    Program p;
    const auto bid = p.inputf64("bid");
    const auto ask = p.inputf64("ask");
    const auto half = p.constant(0.5);
    const auto add_result = p.add(bid, ask);
    p.emit("add_mul", p.mul(half, add_result));
    const auto two = p.constant(2.0);
    const auto difference = p.sub(ask, bid);
    p.emit("sub_mul", p.mul(two, difference));
    const auto three = p.constant(3.0);
    const auto product = p.mul(bid, ask);
    p.emit("mul_add", p.add(three, product));
    const auto second_difference = p.sub(ask, bid);
    p.constant(99.0);
    p.emit("sub_div", p.div(second_difference, bid));

    const ExecutionBlueprint blueprint(p);
    bool has_add_mul = false;
    bool has_sub_mul = false;
    bool has_mul_add = false;
    bool has_sub_div = false;
    for (const Instruction& instruction : blueprint.getInstructions()) {
        require(instruction.op != OpCode::NOP, "NOP was not removed");
        if (instruction.op == OpCode::ADD_MUL_CONST_F64) has_add_mul = true;
        else if (instruction.op == OpCode::SUB_MUL_CONST_F64) has_sub_mul = true;
        else if (instruction.op == OpCode::MUL_ADD_CONST_F64) has_mul_add = true;
        else if (instruction.op == OpCode::SUB_DIV_F64) has_sub_div = true;
    }
    require(has_add_mul && has_sub_mul && has_mul_add && has_sub_div,
        "Expected fused instructions were not created");

    const ExecutionBlueprint unfused_blueprint(p, false);
    for (const Instruction& instruction : unfused_blueprint.getInstructions()) {
        require(instruction.op != OpCode::ADD_MUL_CONST_F64
                && instruction.op != OpCode::SUB_MUL_CONST_F64
                && instruction.op != OpCode::MUL_ADD_CONST_F64
                && instruction.op != OpCode::SUB_DIV_F64,
                "Fusion was used when disabled");
    }

    const std::vector<QuoteRecord> records(845, {100.0, 102.0});
    std::vector<std::vector<double>> values(4, std::vector<double>(records.size()));
    const OutputBuffer outputs[] = {values[0], values[1], values[2], values[3]};
    ChunkedBytecodeExecutor chunked(blueprint, 256);
    chunked.execute(records, outputs);

    BytecodeExecutor scalar(blueprint);
    const auto& scalar_values = scalar.execute(records[0]);
    const auto expected = referenceExecutor(records[0], p);
    for (std::size_t output = 0; output < values.size(); ++output) {
        const double reference = expected.at(blueprint.getOutputNames()[output]);
        require(values[output].front() == reference && values[output].back() == reference,
            "Chunked fused output mismatch");
        require(scalar_values[output] == reference, "Scalar fused output mismatch");
    }

    Program shared;
    const auto shared_bid = shared.inputf64("bid");
    const auto shared_ask = shared.inputf64("ask");
    const auto sum = shared.add(shared_bid, shared_ask);
    const auto midpoint = shared.mul(sum, shared.constant(0.5));
    shared.emit("sum", sum);
    shared.emit("midpoint", midpoint);

    const ExecutionBlueprint fallback(shared);
    for (const Instruction& instruction : fallback.getInstructions()) {
        require(instruction.op != OpCode::ADD_MUL_CONST_F64,
            "Shared intermediate was incorrectly fused");
    }
    BytecodeExecutor fallback_executor(fallback);
    const auto& fallback_values = fallback_executor.execute({100.0, 102.0});
    require(fallback_values[0] == 202.0 && fallback_values[1] == 101.0,
        "Shared intermediate fallback output mismatch");
}

void expectRejection(ChunkedBytecodeExecutor& executor,
                     const std::vector<QuoteRecord>& records,
                     std::vector<double>& output) {
    const auto original = output;
    bool rejected = false;
    try { executor.execute(records, output); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Expected invalid_argument");
    require(output == original, "Invalid call changed output before rejection");
}

void checkInvalidArguments() {
    const ExecutionBlueprint blueprint(makeProgram(Workload::Spread));
    bool rejected = false;
    try { ChunkedBytecodeExecutor executor(blueprint, 0); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Zero capacity accepted");

    rejected = false;
    try {
        ChunkedBytecodeExecutor executor(blueprint, std::numeric_limits<std::size_t>::max());
    } catch (const std::length_error&) { rejected = true; }
    require(rejected, "Impossible scratch size accepted");

    ChunkedBytecodeExecutor executor(blueprint, 4);
    const std::vector<QuoteRecord> records = {{100.0, 103.0}, {200.0, 205.0}};
    std::vector<double> short_output(1, -999.0);
    std::vector<double> long_output(3, -999.0);
    expectRejection(executor, records, short_output);
    expectRejection(executor, records, long_output);

    for (const int output_count : {0, 2}) {
        Program p;
        const auto value = p.constant(1.0);
        if (output_count == 2) {
            p.emit("first", value);
            p.emit("second", value);
        }
        ChunkedBytecodeExecutor unsupported(ExecutionBlueprint(p), 4);
        std::vector<double> output(records.size(), -999.0);
        expectRejection(unsupported, records, output);
        std::vector<double> empty;
        expectRejection(unsupported, {}, empty);
    }
    // Rejected calls must not prevent later valid execution.
    std::vector<double> output(records.size());
    executor.execute(records, output);
    require(output == std::vector<double>({3.0, 5.0}), "Reuse after rejection failed");
}
} // namespace

int main() {
    try {
        checkResults();
        checkFusion();
        checkInvalidArguments();
    } catch (const std::exception& error) {
        std::cerr << "Chunked executor test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
