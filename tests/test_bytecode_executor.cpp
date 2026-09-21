#include "faststreamcompute/bytecode_executor.hpp"
#include "faststreamcompute/program.hpp"
#include "faststreamcompute/record.hpp"
#include "faststreamcompute/reference_executor.hpp"

#include <iostream>

int main() {
    faststreamcompute::Program program;
    const auto bid = program.inputf64("bid");
    program.emit("raw_bid", bid);  // An output node between value-producing nodes.
    const auto ask = program.inputf64("ask");
    const auto sum = program.add(bid, ask);
    const auto spread = program.sub(ask, bid);
    const auto midpoint = program.mul(sum, program.constant(0.5));
    const auto half_ask = program.div(ask, program.constant(2.0));
    program.emit("midpoint", midpoint);
    program.emit("spread", spread);
    program.emit("half_ask", half_ask);
    program.emit("sum", sum);

    faststreamcompute::ExecutionBlueprint blueprint(program);
    faststreamcompute::BytecodeExecutor bytecode(blueprint);
    const auto& names = blueprint.getOutputNames();

    const faststreamcompute::QuoteRecord records[] = {
        {100, 102},
        {200, 204},
        {102, 100},
        {-3, 2},
        {5, 5},
        {0.5, 1.5},
    };

    for (const auto& record : records) {
        const auto expected = faststreamcompute::executor(record, program);
        const auto& actual = bytecode.execute(record);
        if (actual.size() != names.size() || actual.size() != expected.size()) {
            std::cerr << "Output count differs for bid=" << record.bid
                      << ", ask=" << record.ask << '\n';
            return 1;
        }

        for (std::size_t index = 0; index < names.size(); ++index) {
            const auto found = expected.find(names[index]);
            if (found == expected.end() || actual[index] != found->second) {
                std::cerr << "Bytecode mismatch for " << names[index]
                          << " with bid=" << record.bid
                          << ", ask=" << record.ask << '\n';
                if (found != expected.end()) {
                    std::cerr << "Expected " << found->second
                              << ", got " << actual[index] << '\n';
                }
                return 1;
            }
        }
    }

    return 0;
}
