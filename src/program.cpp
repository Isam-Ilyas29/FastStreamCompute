#include "faststreamcompute/program.hpp"

#include <stdexcept>
#include <utility>


namespace faststreamcompute {
    void Program::validateValueOperand(NodeId id) const {
        if (id >= nodes.size()) {
            throw std::out_of_range("operand node does not exist");
        }
        if (nodes[id].operation == Op::OUTPUT) {
            throw std::invalid_argument("output node cannot be used as an operand");
        }
    }

    bool Program::hasOutputName(const std::string& name) const {
        for (const Node& node : nodes) {
            if (node.operation == Op::OUTPUT && node.name == name) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] NodeId Program::inputf64(std::string name) {
        Node n{};
        n.id = nodes.size();
        n.name = std::move(name);
        n.operation = Op::INPUT;
        
        NodeId temp_id = n.id;
        nodes.push_back(std::move(n));
        return temp_id;
    }

    NodeId Program::emit(std::string name, NodeId source) {
        validateValueOperand(source);
        if (hasOutputName(name)) {
            throw std::invalid_argument("output name must be unique");
        }

        Node n{};
        n.id = nodes.size();
        n.name = std::move(name);
        n.operation = Op::OUTPUT;
        n.lhs = source;

        NodeId temp_id = n.id;
        nodes.push_back(std::move(n));
        return temp_id;
    }

    NodeId Program::constant(double val) {
        Node n{};
        n.id = nodes.size();
        n.operation = Op::CONSTANT;
        n.value = val;
        
        NodeId temp_id = n.id;
        nodes.push_back(std::move(n));
        return temp_id;
    }
    
    NodeId Program::add(NodeId id1, NodeId id2) {
        validateValueOperand(id1);
        validateValueOperand(id2);

        Node n{};
        n.id = nodes.size();
        n.operation = Op::ADDITION;
        n.lhs = id1;
        n.rhs = id2;
        
        NodeId temp_id = n.id;
        nodes.push_back(std::move(n));
        return temp_id;
    }

    NodeId Program::sub(NodeId id1, NodeId id2) {
        validateValueOperand(id1);
        validateValueOperand(id2);

        Node n{};
        n.id = nodes.size();
        n.operation = Op::SUBTRACTION;
        n.lhs = id1;
        n.rhs = id2;
        
        NodeId temp_id = n.id;
        nodes.push_back(std::move(n));
        return temp_id;
    }

    NodeId Program::mul(NodeId id1, NodeId id2) {
        validateValueOperand(id1);
        validateValueOperand(id2);

        Node n{};
        n.id = nodes.size();
        n.operation = Op::MULTIPLICATION;
        n.lhs = id1;
        n.rhs = id2;

        NodeId temp_id = n.id;
        nodes.push_back(std::move(n));
        return temp_id;
    }

    NodeId Program::div(NodeId id1, NodeId id2) {
        validateValueOperand(id1);
        validateValueOperand(id2);

        Node n{};
        n.id = nodes.size();
        n.operation = Op::DIVISION;
        n.lhs = id1;
        n.rhs = id2;
        
        NodeId temp_id = n.id;
        nodes.push_back(std::move(n));
        return temp_id;
    }

    const std::vector<Node>& Program::getNodes() const {
        return nodes;
    }
}
