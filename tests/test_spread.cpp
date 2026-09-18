#include "faststreamcompute/executor.hpp"
#include "faststreamcompute/program.hpp"
#include "faststreamcompute/record.hpp"

#include <iostream>

int main() {
    faststreamcompute::Program program;
    const auto bid = program.inputf64("bid");
    const auto ask = program.inputf64("ask");
    const auto spread = program.sub(ask, bid);
    program.emit("spread", spread);

    struct TestCase {
        faststreamcompute::QuoteRecord record;
        double expected_spread;
    };
    const TestCase cases[] = {
        {{100, 102}, 2},
        {{200, 204}, 4},
        {{102, 100}, -2},
        {{-3, 2}, 5},
        {{5, 5}, 0},
    };

    for (const auto& test_case : cases) {
        const auto outputs = faststreamcompute::executor(test_case.record, program);
        const auto found = outputs.find("spread");
        if (outputs.size() != 1 || found == outputs.end() ||
            found->second != test_case.expected_spread) {
            std::cerr << "spread for bid=" << test_case.record.bid
                      << ", ask=" << test_case.record.ask
                      << " should be " << test_case.expected_spread << '\n';
            if (found != outputs.end()) {
                std::cerr << "Actual spread: " << found->second << '\n';
            }
            return 1;
        }
    }

    return 0;
}
