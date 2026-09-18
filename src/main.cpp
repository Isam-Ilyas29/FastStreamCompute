#include "faststreamcompute/program.hpp"
#include "faststreamcompute/record.hpp"
#include "faststreamcompute/reference_executor.hpp"
#include "faststreamcompute/bytecode_exector.hpp"

#include <iostream>
#include <vector>
#include <unordered_map>


int main() {
    faststreamcompute::Program p_midpoint;
    auto bid1 = p_midpoint.inputf64("bid");
    auto ask1 = p_midpoint.inputf64("ask");
    auto midpoint = p_midpoint.mul(
        p_midpoint.add(bid1, ask1),
        p_midpoint.constant(0.5));
    p_midpoint.emit("midpoint", midpoint);

    faststreamcompute::Program p_spread;
    auto bid2 = p_spread.inputf64("bid");
    auto ask2 = p_spread.inputf64("ask");
    auto spread = p_spread.sub(ask2, bid2);
    p_spread.emit("spread", spread);

    std::vector<faststreamcompute::QuoteRecord> records = {{100, 102}, {200, 204}};

    // auto results1 = faststreamcompute::executor(records[0], p_midpoint);

    // for (const auto& [key, value] : results1) {
    //     std::cout << "(" << key << ", " << value << ")\n";
    // }

    // auto results2 = faststreamcompute::executor(records[1], p_spread);

    // for (const auto& [key, value] : results2) {
    //     std::cout << "(" << key << ", " << value << ")\n";
    // }

    faststreamcompute::ExecutionBlueprint bp(p_midpoint);
    faststreamcompute::BytecodeExecutor executor(bp);

    for (const auto& r : records) {
        const auto& output = executor.execute(r);
        for (const auto& o: output) {
            std::cout << o << '\n';
        }
    }
}




