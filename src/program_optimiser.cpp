#include "faststreamcompute/program_optimiser.hpp"

#include <vector>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <map>


namespace faststreamcompute {
    static void foldConstants(std::vector<Node>& nodes) {
        for (Node& node : nodes) {
            if (node.operation == Op::ADDITION || node.operation == Op::SUBTRACTION || node.operation == Op::MULTIPLICATION || node.operation == Op::DIVISION) {
                const Node& lhs = nodes[node.lhs];
                const Node& rhs = nodes[node.rhs];
                if (lhs.operation == Op::CONSTANT && rhs.operation == Op::CONSTANT) {
                    double result;
                    if (node.operation == Op::ADDITION) {
                        result = lhs.value + rhs.value;
                    }
                    else if (node.operation == Op::SUBTRACTION) {
                        result = lhs.value - rhs.value;
                    }
                    else if (node.operation == Op::MULTIPLICATION) {
                        result = lhs.value * rhs.value;
                    }
                    else {
                        result = lhs.value / rhs.value;
                    }

                    // Keep the ID and position so later operands still refer to this node.
                    node.operation = Op::CONSTANT;
                    node.value = result;
                    node.lhs = 0;
                    node.rhs = 0;
                }
            }
        }
    }

    static Program rebuildProgram(const std::vector<Node>& nodes, const std::vector<bool>& required, bool eliminate_common_subexpressions) {
        Program rebuilt;
        // Track lhs (and rhs) for arithmetic and output nodes when constructing new program
        std::vector<std::optional<NodeId>> new_node_ids(nodes.size());
        using ExpressionKey = std::tuple<Op, NodeId, NodeId>;
        std::map<ExpressionKey, NodeId> expressions;

        for (const Node& node : nodes) {
            if (required[node.id]) {
                NodeId new_id;

                if (node.operation == Op::INPUT) {
                    new_id = rebuilt.inputf64(node.name);
                }
                else if (node.operation == Op::CONSTANT) {
                    new_id = rebuilt.constant(node.value);
                }
                else if (node.operation == Op::OUTPUT) {
                    NodeId new_source = new_node_ids[node.lhs].value();
                    new_id = rebuilt.emit(node.name, new_source);
                }
                else if (node.operation == Op::ADDITION || node.operation == Op::SUBTRACTION || node.operation == Op::MULTIPLICATION || node.operation == Op::DIVISION) {
                    NodeId new_lhs = new_node_ids[node.lhs].value();
                    NodeId new_rhs = new_node_ids[node.rhs].value();

                    const ExpressionKey key{node.operation, new_lhs, new_rhs};
                    auto found = expressions.end();
                    if (eliminate_common_subexpressions) {
                        found = expressions.find(key);
                    }

                    if (found != expressions.end()) {
                        new_id = found->second;
                    }
                    else {
                        if (node.operation == Op::ADDITION) {
                            new_id = rebuilt.add(new_lhs, new_rhs);
                        }
                        else if (node.operation == Op::SUBTRACTION) {
                            new_id = rebuilt.sub(new_lhs, new_rhs);
                        }
                        else if (node.operation == Op::MULTIPLICATION) {
                            new_id = rebuilt.mul(new_lhs, new_rhs);
                        }
                        else {
                            new_id = rebuilt.div(new_lhs, new_rhs);
                        }
                        if (eliminate_common_subexpressions) {
                            expressions.emplace(key, new_id);
                        }
                    }
                }
                else {
                    throw std::logic_error("invalid operation type");
                }

                new_node_ids[node.id] = new_id;
            }
        }

        return rebuilt;
    }

    static std::vector<bool> findRequiredNodes(const std::vector<Node>& nodes) {
        std::vector<bool> required(nodes.size(), false);

        // Operands appear before their users, so mark dependencies in reverse.
        for (std::size_t index = nodes.size(); index > 0; --index) {
            const Node& node = nodes[index - 1];

            if (node.operation == Op::OUTPUT) {
                required[node.id] = true;
                required[node.lhs] = true;
            }
            else if (required[node.id]) {
                if (node.operation == Op::ADDITION || node.operation == Op::SUBTRACTION || node.operation == Op::MULTIPLICATION || node.operation == Op::DIVISION) {
                    required[node.lhs] = true;
                    required[node.rhs] = true;
                }
            }
        }

        return required;
    }

    Program optimiseProgram(const Program& original, OptimisationOptions options) {
        if (!options.remove_dead_nodes && !options.fold_constants && !options.eliminate_common_subexpressions) {
            return original;
        }

        std::vector<Node> nodes = original.getNodes();
        if (options.fold_constants) {
            foldConstants(nodes);
        }

        const auto required = options.remove_dead_nodes
            ? findRequiredNodes(nodes) : std::vector<bool>(nodes.size(), true);
        return rebuildProgram(nodes, required, options.eliminate_common_subexpressions);
    }
}
