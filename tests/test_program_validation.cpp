#include "faststreamcompute/bytecode_executor.hpp"
#include "faststreamcompute/bytecode_executor.hpp"  // Intentional include-guard check.

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

template <typename Function>
bool expect_invalid_argument(Function function, const std::string& description) {
    try {
        function();
    }
    catch (const std::invalid_argument&) {
        return true;
    }
    catch (...) {
        std::cerr << description << " threw the wrong exception type\n";
        return false;
    }

    std::cerr << description << " was accepted\n";
    return false;
}

template <typename Function>
bool expect_out_of_range(Function function, const std::string& description) {
    try {
        function();
    }
    catch (const std::out_of_range&) {
        return true;
    }
    catch (...) {
        std::cerr << description << " threw the wrong exception type\n";
        return false;
    }

    std::cerr << description << " was accepted\n";
    return false;
}

}  // namespace

int main() {
    {
        faststreamcompute::Program program;
        const auto bid = program.inputf64("bid");
        program.emit("raw_bid", bid);
        program.emit("raw_bid_copy", bid);
        const auto doubled_bid = program.add(bid, bid);
        program.emit("doubled_bid", doubled_bid);

        faststreamcompute::ExecutionBlueprint blueprint(program);
        faststreamcompute::BytecodeExecutor executor(blueprint);
        const auto& outputs = executor.execute({100.0, 102.0});
        if (outputs.size() != 3 || outputs[0] != 100.0 ||
            outputs[1] != 100.0 || outputs[2] != 200.0) {
            std::cerr << "original values were not reusable after emit\n";
            return 1;
        }
    }

    {
        faststreamcompute::Program program;
        const auto bid = program.inputf64("bid");
        const auto raw_bid_output = program.emit("raw_bid", bid);

        using BinaryOperation = faststreamcompute::NodeId (
            faststreamcompute::Program::*)(faststreamcompute::NodeId,
                                           faststreamcompute::NodeId);
        struct OperationCase {
            const char* name;
            BinaryOperation operation;
        };
        const OperationCase operations[] = {
            {"add", &faststreamcompute::Program::add},
            {"subtract", &faststreamcompute::Program::sub},
            {"multiply", &faststreamcompute::Program::mul},
            {"divide", &faststreamcompute::Program::div},
        };

        for (const auto& operation_case : operations) {
            if (!expect_invalid_argument(
                    [&] {
                        (program.*operation_case.operation)(raw_bid_output, bid);
                    },
                    std::string(operation_case.name) +
                        " with an output node on the left")) {
                return 1;
            }
            if (!expect_invalid_argument(
                    [&] {
                        (program.*operation_case.operation)(bid, raw_bid_output);
                    },
                    std::string(operation_case.name) +
                        " with an output node on the right")) {
                return 1;
            }
        }

        if (!expect_invalid_argument(
                [&] { program.emit("copied_output", raw_bid_output); },
                "emitting an output node")) {
            return 1;
        }
    }

    {
        faststreamcompute::Program program;
        const auto bid = program.inputf64("bid");
        const faststreamcompute::NodeId missing = program.getNodes().size();

        using BinaryOperation = faststreamcompute::NodeId (
            faststreamcompute::Program::*)(faststreamcompute::NodeId,
                                           faststreamcompute::NodeId);
        struct OperationCase {
            const char* name;
            BinaryOperation operation;
        };
        const OperationCase operations[] = {
            {"add", &faststreamcompute::Program::add},
            {"subtract", &faststreamcompute::Program::sub},
            {"multiply", &faststreamcompute::Program::mul},
            {"divide", &faststreamcompute::Program::div},
        };

        for (const auto& operation_case : operations) {
            if (!expect_out_of_range(
                    [&] { (program.*operation_case.operation)(missing, bid); },
                    std::string(operation_case.name) +
                        " with a missing left operand")) {
                return 1;
            }
            if (!expect_out_of_range(
                    [&] { (program.*operation_case.operation)(bid, missing); },
                    std::string(operation_case.name) +
                        " with a missing right operand")) {
                return 1;
            }
        }

        if (!expect_out_of_range(
                [&] { program.emit("missing", missing); },
                "emitting a missing operand")) {
            return 1;
        }
    }

    {
        faststreamcompute::Program program;
        const auto bid = program.inputf64("bid");
        const auto ask = program.inputf64("ask");
        program.emit("value", bid);

        if (!expect_invalid_argument(
                [&] { program.emit("value", ask); },
                "using a duplicate output name")) {
            return 1;
        }
    }

    return 0;
}
