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
    if (!condition) throw std::runtime_error(message);
}

Program makeProgram() {
    Program p;
    const auto bid = p.inputf64("bid");
    const auto ask = p.inputf64("ask");
    const auto spread = p.sub(ask, bid);
    p.emit("spread", spread);
    // Later outputs reuse earlier calculations, even across emit instructions.
    const auto sum = p.add(bid, ask);
    p.emit("midpoint", p.mul(sum, p.constant(0.5)));
    p.emit("relative_spread", p.div(spread, sum));
    p.emit("spread_again", spread);
    return p;
}

void checkResults() {
    const auto program = makeProgram();
    const ExecutionBlueprint blueprint(program);
    for (const std::size_t capacity : {1, 7, 256}) {
        ChunkedBytecodeExecutor chunked(blueprint, capacity);
        BytecodeExecutor scalar(blueprint);
        const std::vector<std::size_t> sizes = {
            0, 1, capacity - 1, capacity, capacity + 1, 2 * capacity, 845, 0, 1
        };
        for (const auto size : sizes) {
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<QuoteRecord> records(size);
                for (std::size_t row = 0; row < size; ++row) {
                    const double bid = 100.0 + static_cast<double>(row);
                    records[row] = {bid, bid + (pass == 0 ? 2.0 : -3.0)};
                }
                const double unset = std::numeric_limits<double>::quiet_NaN();
                std::vector<std::vector<double>> chunk_values(4, std::vector<double>(size, unset));
                std::vector<std::vector<double>> batch_values(4, std::vector<double>(size, unset));
                const OutputBuffer chunk_outputs[] = {
                    chunk_values[0], chunk_values[1], chunk_values[2], chunk_values[3]
                };
                const OutputBuffer batch_outputs[] = {
                    batch_values[0], batch_values[1], batch_values[2], batch_values[3]
                };
                chunked.execute(records, chunk_outputs);
                batchExecute(scalar, records, batch_outputs);

                for (std::size_t row = 0; row < size; ++row) {
                    const auto reference = referenceExecutor(records[row], program);
                    const double spread = records[row].ask - records[row].bid;
                    const double sum = records[row].bid + records[row].ask;
                    const double expected[] = {spread, sum * 0.5, spread / sum, spread};
                    for (std::size_t slot = 0; slot < 4; ++slot) {
                        require(chunk_values[slot][row] == expected[slot], "chunk output differs from formula");
                        require(batch_values[slot][row] == expected[slot], "batch output differs from formula");
                        require(expected[slot] == reference.at(blueprint.getOutputNames()[slot]), "reference mismatch");
                    }
                }
            }
        }
    }
}

void checkInvalidBuffers() {
    const ExecutionBlueprint blueprint(makeProgram());
    ChunkedBytecodeExecutor chunked(blueprint, 256);
    BytecodeExecutor scalar(blueprint);
    const std::vector<QuoteRecord> records(845, {100.0, 102.0});
    std::vector<double> first(845, -99.0), second(845, -99.0);
    std::vector<double> third(845, -99.0), fourth(845, -99.0);
    const OutputBuffer buffers[] = {first, second, third, fourth};
    const OutputBuffer short_buffers[] = {first, second, third, std::span<double>(fourth).first(844)};
    std::vector<double> longer(846, -99.0);
    const OutputBuffer long_buffers[] = {first, second, third, longer};
    const OutputBuffer extra_buffers[] = {first, second, third, fourth, longer};

    for (bool use_chunked : {false, true}) {
        const auto run = [&](std::span<const OutputBuffer> outputs) {
            if (use_chunked) chunked.execute(records, outputs);
            else batchExecute(scalar, records, outputs);
        };
        const auto reject = [&](std::span<const OutputBuffer> outputs) {
            bool rejected = false;
            try { run(outputs); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "invalid buffers were accepted");
            for (const auto buffer : buffers) {
                for (double value : buffer) require(value == -99.0, "validation wrote an output");
            }
            for (double value : longer) require(value == -99.0, "validation wrote long buffer");
        };
        reject(std::span<const OutputBuffer>(buffers).first(3));
        reject(extra_buffers);
        reject(short_buffers);
        reject(long_buffers);
    }
    // Rejection must leave both executors usable.
    chunked.execute(records, buffers);
    require(first.back() == 2.0 && second.back() == 101.0, "chunk reuse failed");
    batchExecute(scalar, records, buffers);
    require(first.back() == 2.0 && second.back() == 101.0, "batch reuse failed");
}

void checkNoOutputs() {
    Program p;
    p.add(p.inputf64("bid"), p.inputf64("ask"));
    const ExecutionBlueprint blueprint(p);
    ChunkedBytecodeExecutor chunked(blueprint, 256);
    BytecodeExecutor scalar(blueprint);
    const std::span<const OutputBuffer> outputs;
    for (const std::size_t size : {0, 845}) {
        const std::vector<QuoteRecord> records(size, {100.0, 102.0});
        chunked.execute(records, outputs);
        batchExecute(scalar, records, outputs);
    }
}
}

int main() {
    try {
        checkResults();
        checkInvalidBuffers();
        checkNoOutputs();
    } catch (const std::exception& error) {
        std::cerr << "Multi-output test failed: " << error.what() << '\n';
        return 1;
    }
}
