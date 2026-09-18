#include "faststreamcompute/executor.hpp"
#include "faststreamcompute/program.hpp"
#include "faststreamcompute/record.hpp"

#include <iostream>
#include <string>
#include <unordered_map>

namespace {

bool expect_output(const std::unordered_map<std::string, double>& outputs,
                   const std::string& name, double expected) {
    const auto found = outputs.find(name);
    if (found == outputs.end()) {
        std::cerr << "Missing output: " << name << '\n';
        return false;
    }
    if (found->second != expected) {
        std::cerr << name << " was " << found->second
                  << ", expected " << expected << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    faststreamcompute::Program program;
    const auto bid = program.inputf64("bid");
    const auto ask = program.inputf64("ask");
    const auto sum = program.add(bid, ask);
    const auto half = program.constant(0.5);
    const auto midpoint = program.mul(sum, half);
    program.emit("midpoint", midpoint);
    program.emit("sum", sum);

    struct TestCase {
        faststreamcompute::QuoteRecord record;
        double expected_midpoint;
        double expected_sum;
    };
    const TestCase cases[] = {
        {{100, 102}, 101, 202},
        {{200, 204}, 202, 404},
        {{100, 101}, 100.5, 201},
        {{-2, 2}, 0, 0},
    };

    for (const auto& test_case : cases) {
        const auto outputs = faststreamcompute::executor(test_case.record, program);
        if (outputs.size() != 2 ||
            !expect_output(outputs, "midpoint", test_case.expected_midpoint) ||
            !expect_output(outputs, "sum", test_case.expected_sum)) {
            std::cerr << "For record bid=" << test_case.record.bid
                      << ", ask=" << test_case.record.ask << '\n';
            return 1;
        }
    }

    return 0;
}
