#include "faststreamcompute/program.hpp"

#include <stdexcept>
#include <utility>


namespace faststreamcompute {
    NodeId Program::inputf64(std::string name) {
        Node n{};
        n.id = nodes.size();
        n.name = std::move(name);
        n.operation = Op::INPUT;
        
        NodeId temp_id = n.id;
        nodes.push_back(std::move(n));
        return temp_id;
    }

    NodeId Program::emit(std::string name, NodeId source) {
        if (source >= nodes.size()) {
            throw std::out_of_range("operand node does not exist");
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
        if (!(id1 < nodes.size() && id2 < nodes.size())) {
            throw std::out_of_range("operand node does not exist");
        }

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
        if (!(id1 < nodes.size() && id2 < nodes.size())) {
            throw std::out_of_range("operand node does not exist");
        }

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
        if (!(id1 < nodes.size() && id2 < nodes.size())) {
            throw std::out_of_range("operand node does not exist");
        }

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
        if (!(id1 < nodes.size() && id2 < nodes.size())) {
            throw std::out_of_range("operand node does not exist");
        }

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
