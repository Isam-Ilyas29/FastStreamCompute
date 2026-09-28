#include "faststreamcompute/program_optimiser.hpp"
#include "faststreamcompute/bytecode_executor.hpp"
#include "faststreamcompute/reference_executor.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>
#include <cmath>
#include <limits>

namespace {
    using namespace faststreamcompute;

    void require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }

    bool sameValue(double lhs, double rhs) {
        if (std::isnan(lhs) || std::isnan(rhs)) return std::isnan(lhs) && std::isnan(rhs);
        if (lhs == 0.0 && rhs == 0.0) return std::signbit(lhs) == std::signbit(rhs);
        return lhs == rhs;
    }

    void checkOutputs(const Program& original, const Program& optimised) {
        ExecutionBlueprint blueprint(optimised);
        BytecodeExecutor scalar(blueprint);
        ChunkedBytecodeExecutor chunked(blueprint, 256);
        ExecutionBlueprint original_blueprint(original);
        BytecodeExecutor original_scalar(original_blueprint);
        ChunkedBytecodeExecutor original_chunked(original_blueprint, 256);
        require(blueprint.getOutputNames() == original_blueprint.getOutputNames(), "output names or order changed");
        std::vector<QuoteRecord> records(845);
        for (std::size_t row = 0; row < records.size(); ++row) {
            records[row] = {static_cast<double>(row) - 400.0, static_cast<double>(row) + 2.5};
        }
        records[0] = {0.0, -0.0};
        records[1] = {std::numeric_limits<double>::infinity(), 2.0};
        records[2] = {std::numeric_limits<double>::quiet_NaN(), 3.0};
        records[3] = {1e300, -1e300};
        records[4] = {1e-300, -1e-300};
        std::vector<std::vector<double>> values(blueprint.getOutputNames().size(), std::vector<double>(records.size()));
        std::vector<OutputBuffer> outputs;
        for (auto& buffer : values) outputs.push_back(buffer);
        chunked.execute(records, outputs);
        std::vector<std::vector<double>> original_values(blueprint.getOutputNames().size(), std::vector<double>(records.size()));
        std::vector<OutputBuffer> original_outputs;
        for (auto& buffer : original_values) original_outputs.push_back(buffer);
        original_chunked.execute(records, original_outputs);

        for (std::size_t row = 0; row < records.size(); ++row) {
            const auto expected = referenceExecutor(records[row], original);
            const auto rebuilt_reference = referenceExecutor(records[row], optimised);
            require(rebuilt_reference.size() == expected.size(), "reference output count changed");
            const auto& actual = scalar.execute(records[row]);
            const auto& original_actual = original_scalar.execute(records[row]);
            require(actual.size() == expected.size(), "output count changed");
            for (std::size_t slot = 0; slot < actual.size(); ++slot) {
                const double value = expected.at(blueprint.getOutputNames()[slot]);
                require(sameValue(rebuilt_reference.at(blueprint.getOutputNames()[slot]), value), "reference output changed");
                require(sameValue(actual[slot], value), "scalar output changed");
                require(sameValue(values[slot][row], value), "chunked output changed");
                require(sameValue(original_actual[slot], value), "original scalar differs from reference");
                require(sameValue(original_values[slot][row], value), "original chunked differs from reference");
            }
        }
    }

    void checkDeadNodes() {
        Program p;
        auto bid = p.inputf64("bid");
        auto unused = p.constant(99.0);
        auto ask = p.inputf64("ask");
        auto dead = p.mul(ask, unused);
        p.div(dead, unused);
        auto spread = p.sub(ask, bid);
        p.emit("spread", spread);
        auto sum = p.add(bid, bid);
        auto copied_bid = p.mul(sum, p.constant(0.5));
        p.emit("bid_again", copied_bid);
        p.emit("spread_again", spread);
        p.add(ask, unused);

        const Program unchanged = optimiseProgram(p, {
            .remove_dead_nodes = false,
            .fold_constants = false,
            .eliminate_common_subexpressions = false
        });
        require(unchanged.getNodes().size() == p.getNodes().size(), "disabled dead-node removal changed graph size");

        const Program result = optimiseProgram(p, {.fold_constants = false, .eliminate_common_subexpressions = false});
        require(p.getNodes().size() == 13, "original program changed");
        require(result.getNodes().size() == 9, "dead chain was not removed");
        require(result.getNodes()[2].lhs == 1 && result.getNodes()[2].rhs == 0, "subtraction IDs not remapped");
        const ExecutionBlueprint blueprint(result);
        require(blueprint.getOutputNames() == std::vector<std::string>{"spread", "bid_again", "spread_again"}, "output order changed");
        checkOutputs(p, result);
        require(optimiseProgram(result).getNodes().size() == 9, "second pass removed live nodes");
    }

    void checkEdgeCases() {
        require(optimiseProgram(Program{}).getNodes().empty(), "empty program failed");

        Program no_outputs;
        no_outputs.add(no_outputs.inputf64("bid"), no_outputs.constant(2.0));
        require(optimiseProgram(no_outputs).getNodes().empty(), "output-free program retained nodes");

        Program constant;
        (void)constant.inputf64("ask");
        constant.emit("answer", constant.constant(42.0));
        const Program result = optimiseProgram(constant);
        require(result.getNodes().size() == 2, "unused input retained");
        checkOutputs(constant, result);

        Program division;
        division.emit("ratio", division.div(division.inputf64("ask"), division.inputf64("bid")));
        const Program unchanged = optimiseProgram(division);
        require(unchanged.getNodes().size() == division.getNodes().size(), "live division removed");
        checkOutputs(division, unchanged);
    }

    void checkFolding() {
        using BinaryOperation = NodeId (Program::*)(NodeId, NodeId);
        const BinaryOperation operations[] = {&Program::add, &Program::sub, &Program::mul, &Program::div};
        for (const auto operation : operations) {
            Program p;
            auto lhs = p.constant(6.0);
            auto rhs = p.constant(2.0);
            p.emit("answer", (p.*operation)(lhs, rhs));
            const Program control = optimiseProgram(p, {false, false, false});
            const Program folding_only = optimiseProgram(p, {.remove_dead_nodes = false, .eliminate_common_subexpressions = false});
            const Program folded = optimiseProgram(p);
            require(control.getNodes().size() == 4, "disabled folding changed arithmetic");
            require(folding_only.getNodes().size() == 4, "fold-only pass unexpectedly removed dead constants");
            require(folding_only.getNodes()[2].operation == Op::CONSTANT, "fold-only pass did not fold arithmetic");
            require(folded.getNodes().size() == 2, "constant arithmetic or old constants remain");
            require(folded.getNodes()[0].operation == Op::CONSTANT, "result is not a constant");
            checkOutputs(p, folded);
            checkOutputs(p, folding_only);
        }

        Program chain;
        auto two = chain.constant(2.0);
        auto three = chain.constant(3.0);
        auto six = chain.mul(two, three);
        chain.emit("six", six);
        auto ten = chain.add(six, chain.constant(4.0));
        chain.emit("ten", ten);
        chain.emit("six_again", six);
        chain.emit("mixed", chain.mul(chain.inputf64("bid"), ten));
        const Program folded = optimiseProgram(chain);
        require(chain.getNodes().size() == 11, "original changed during folding");
        require(folded.getNodes().size() == 8, "chain folding or cleanup failed");
        require(folded.getNodes()[0].value == 6.0 && folded.getNodes()[2].value == 10.0, "chain values wrong");
        require(folded.getNodes()[6].operation == Op::MULTIPLICATION, "input-dependent arithmetic folded");
        checkOutputs(chain, folded);
    }

    void checkWorkingCopy() {
        Program p;
        p.constant(99.0);
        auto bid = p.inputf64("bid");
        auto two = p.constant(2.0);
        auto three = p.constant(3.0);
        auto six = p.mul(two, three);
        p.emit("answer", p.add(bid, six));
        const auto before = p.getNodes();

        const Program result = optimiseProgram(p);
        const auto& nodes = result.getNodes();
        require(nodes.size() == 4, "working copy was not compacted");
        require(nodes[0].operation == Op::INPUT && nodes[0].name == "bid", "input changed");
        require(nodes[1].operation == Op::CONSTANT && nodes[1].value == 6.0, "folded constant missing");
        require(nodes[2].operation == Op::ADDITION && nodes[2].lhs == 0 && nodes[2].rhs == 1, "addition mapping wrong");
        require(nodes[3].operation == Op::OUTPUT && nodes[3].lhs == 2, "output mapping wrong");
        for (std::size_t i = 0; i < before.size(); ++i) {
            const Node& old = before[i];
            const Node& current = p.getNodes()[i];
            require(old.id == current.id && old.name == current.name && old.operation == current.operation
                && old.value == current.value && old.lhs == current.lhs && old.rhs == current.rhs, "original node modified");
        }
        checkOutputs(p, result);
    }

    void checkCSE() {
        const OptimisationOptions cse_only{false, false, true};
        using BinaryOperation = NodeId (Program::*)(NodeId, NodeId);
        const BinaryOperation operations[] = {&Program::add, &Program::sub, &Program::mul, &Program::div};
        for (const auto operation : operations) {
            Program p;
            auto bid = p.inputf64("bid");
            auto ask = p.inputf64("ask");
            p.emit("first", (p.*operation)(bid, ask));
            p.emit("duplicate", (p.*operation)(bid, ask));
            p.emit("reversed", (p.*operation)(ask, bid));
            const Program result = optimiseProgram(p, cse_only);
            require(result.getNodes().size() == 7, "CSE did not merge exactly one ordered duplicate");
            require(result.getNodes()[3].lhs == result.getNodes()[4].lhs, "outputs do not share their result");
            checkOutputs(p, result);
        }

        Program nested;
        auto bid = nested.inputf64("bid");
        auto ask = nested.inputf64("ask");
        auto sum = nested.add(bid, ask);
        nested.emit("early", sum);
        auto duplicate = nested.add(bid, ask);
        auto twice = nested.add(sum, sum);
        auto twice_again = nested.add(duplicate, duplicate);
        nested.emit("twice", twice);
        nested.emit("twice_again", twice_again);
        const Program result = optimiseProgram(nested, cse_only);
        require(result.getNodes().size() == 7, "nested duplicates were not merged");
        checkOutputs(nested, result);

        Program distinct;
        bid = distinct.inputf64("bid");
        ask = distinct.inputf64("ask");
        distinct.emit("add", distinct.add(bid, ask));
        distinct.emit("sub", distinct.sub(bid, ask));
        const Program separate = optimiseProgram(distinct, cse_only);
        require(separate.getNodes().size() == distinct.getNodes().size(), "different operations merged");
        checkOutputs(distinct, separate);
    }

    void checkOptions() {
        Program p;
        p.constant(99.0);
        auto bid = p.inputf64("bid");
        auto ask = p.inputf64("ask");
        auto two = p.constant(2.0);
        auto three = p.constant(3.0);
        auto six = p.mul(two, three);
        auto sum = p.add(bid, ask);
        p.emit("sum", sum);
        auto duplicate = p.add(bid, ask);
        p.emit("weighted", p.mul(duplicate, six));
        p.mul(ask, two);
        const auto before = p.getNodes();

        for (bool dead : {false, true}) {
            for (bool fold : {false, true}) {
                for (bool cse : {false, true}) {
                    const Program result = optimiseProgram(p, {dead, fold, cse});
                    const std::size_t expected_size = 12u - (dead ? 2u : 0u)
                        - (dead && fold ? 2u : 0u) - (cse ? 1u : 0u);
                    require(result.getNodes().size() == expected_size, "option combination changed wrong nodes");
                    checkOutputs(p, result);
                    require(optimiseProgram(Program{}, {dead, fold, cse}).getNodes().empty(), "empty program failed with options");
                }
            }
        }
        for (std::size_t i = 0; i < before.size(); ++i) {
            const Node& old = before[i];
            const Node& current = p.getNodes()[i];
            require(old.id == current.id && old.name == current.name && old.operation == current.operation
                && old.value == current.value && old.lhs == current.lhs && old.rhs == current.rhs, "options modified original");
        }
    }

    void checkNumericEdges() {
        using BinaryOperation = NodeId (Program::*)(NodeId, NodeId);
        struct Case { double lhs; double rhs; BinaryOperation operation; };
        const double inf = std::numeric_limits<double>::infinity();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double largest = std::numeric_limits<double>::max();
        const double smallest_normal = std::numeric_limits<double>::min();
        const Case cases[] = {
            {-0.0, 2.0, &Program::mul},
            {1.0, 0.0, &Program::div},
            {1.0, -0.0, &Program::div},
            {0.0, 0.0, &Program::div},
            {inf, 2.0, &Program::add},
            {nan, 2.0, &Program::mul},
            {largest, 2.0, &Program::mul},
            {largest, 2.0, &Program::div},
            {smallest_normal, 2.0, &Program::div}
        };
        for (const auto& test : cases) {
            Program p;
            auto lhs = p.constant(test.lhs);
            auto rhs = p.constant(test.rhs);
            p.emit("answer", (p.*test.operation)(lhs, rhs));
            const Program result = optimiseProgram(p);
            require(result.getNodes().size() == 2, "constant numeric edge was not folded");
            checkOutputs(p, result);
        }
    }
}

int main() {
    try {
        checkDeadNodes();
        checkEdgeCases();
        checkFolding();
        checkWorkingCopy();
        checkCSE();
        checkOptions();
        checkNumericEdges();
    } catch (const std::exception& error) {
        std::cerr << "Optimiser test failed: " << error.what() << '\n';
        return 1;
    }
}
