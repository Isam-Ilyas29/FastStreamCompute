#pragma once

#include <vector>
#include <string>
#include <cstddef>

namespace faststreamcompute {
    enum class Op {
        INPUT, 
        OUTPUT,
        CONSTANT, 
        ADDITION,
        SUBTRACTION,
        MULTIPLICATION,
        DIVISION
    };

    using NodeId = std::size_t;

    struct Node {
        NodeId id;
        std::string name;
        Op operation;
        double value;
        NodeId lhs;
        NodeId rhs;
    };

    class Program {
        private:
            std::vector<Node> nodes;

            void validateValueOperand(NodeId id) const;
            bool hasOutputName(const std::string& name) const;

        public:
            // Return node's ID
            [[nodiscard]] NodeId inputf64(std::string name);

            NodeId emit(std::string name, NodeId source);

            NodeId constant(double val);

            NodeId add(NodeId id1, NodeId id2);
            NodeId sub(NodeId id1, NodeId id2);
            NodeId mul(NodeId id1, NodeId id2);
            NodeId div(NodeId id1, NodeId id2);

            const std::vector<Node>& getNodes() const;
    };
}
