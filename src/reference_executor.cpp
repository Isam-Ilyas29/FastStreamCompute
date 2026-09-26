#include "faststreamcompute/reference_executor.hpp"

#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <string>


namespace faststreamcompute {
    std::unordered_map<std::string, double> referenceExecutor(const QuoteRecord& record, const Program& program) {
        std::unordered_map<std::string, double> results;
        std::vector<double> values;

        for (const Node& node : program.getNodes()) {
            if (node.operation == Op::CONSTANT) {
                values.push_back(node.value);
            }
            else if (node.operation == Op::INPUT) {
                if (node.name == "bid") {
                    values.push_back(record.bid);
                }
                else if (node.name  == "ask") {
                    values.push_back(record.ask);
                }
                else {
                    throw std::logic_error("input name is invalid");
                }
            }
            else if (node.operation == Op::ADDITION || node.operation == Op::SUBTRACTION || node.operation == Op::MULTIPLICATION || node.operation == Op::DIVISION) {
                double lhs = values[node.lhs];
                double rhs = values[node.rhs];
                if (node.operation == Op::ADDITION) {
                    values.push_back(lhs+rhs);
                }
                else if (node.operation == Op::SUBTRACTION) {
                    values.push_back(lhs-rhs);
                }
                else if (node.operation == Op::MULTIPLICATION) {
                    values.push_back(lhs*rhs);
                }
                else if (node.operation == Op::DIVISION) {
                    values.push_back(lhs/rhs);
                }
            }
            else if (node.operation == Op::OUTPUT) {
                values.push_back(values[node.lhs]);
                results[node.name] = values[node.id];
            }
        }

        return results;
    }
}
