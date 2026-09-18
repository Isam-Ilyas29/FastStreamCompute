#include "faststreamcompute/bytecode_exector.hpp"

#include <stdexcept>


namespace faststreamcompute {
    ExecutionBlueprint::ExecutionBlueprint(const Program& p) {
        for (const Node& node: p.getNodes()) {
            if (node.operation == Op::INPUT) {
                std::size_t field_id;
                if (node.name == "bid") {
                    field_id = static_cast<std::size_t>(InputField::Bid);
                }
                else if (node.name == "ask") {
                    field_id = static_cast<std::size_t>(InputField::Ask);
                }
                else {
                    throw std::logic_error("input node name is invalid");
                }
                instructions.push_back(Instruction{OpCode::LOAD_F64, node.id, 0, 0, field_id});
            }
            else if (node.operation == Op::CONSTANT) {
                constants.push_back(node.value);
                instructions.push_back(Instruction{OpCode::CONST_F64, node.id, 0, 0, constants.size()-1});
            }
            else if (node.operation == Op::ADDITION) {
                instructions.push_back(Instruction{OpCode::ADD_F64, node.id, node.lhs, node.rhs, 0});
            }
            else if (node.operation == Op::SUBTRACTION) {
                instructions.push_back(Instruction{OpCode::SUB_F64, node.id, node.lhs, node.rhs, 0});
            }
            else if (node.operation == Op::MULTIPLICATION) {
                instructions.push_back(Instruction{OpCode::MUL_F64, node.id, node.lhs, node.rhs, 0});
            }
            else if (node.operation == Op::DIVISION) {
                instructions.push_back(Instruction{OpCode::DIV_F64, node.id, node.lhs, node.rhs, 0});
            }
            else if (node.operation == Op::OUTPUT) {
                output_names.push_back(node.name);
                instructions.push_back(Instruction{OpCode::STORE_OUTPUT_F64, 0, node.lhs, 0, output_names.size()-1});
            }
        }
        register_count = p.getNodes().size();
    }

    const std::vector<Instruction>& ExecutionBlueprint::getInstructions() const {
        return instructions;
    }
    const std::vector<double>& ExecutionBlueprint::getConstants() const {
        return constants;
    }
    const std::vector<std::string>& ExecutionBlueprint::getOutputNames() const {
        return output_names;
    }
    const int& ExecutionBlueprint::getRegisterCount() const {
        return register_count;
    }

    BytecodeExecutor::BytecodeExecutor(ExecutionBlueprint bp) : blueprint(bp), registers(bp.getRegisterCount()), outputs(bp.getOutputNames().size()) { }

    const std::vector<double>& BytecodeExecutor::execute(const QuoteRecord& record) {
        for (const Instruction& i: blueprint.getInstructions()) {
            switch (i.op) {
                case OpCode::NOP:
                    // No Operation
                    break;
                case OpCode::LOAD_F64: {
                    const InputField field = static_cast<InputField>(i.auxiliary);
                    if (field == InputField::Bid) {
                        registers[i.dst] = record.bid;
                    }
                    else if (field == InputField::Ask) {
                        registers[i.dst] = record.ask;
                    }
                    break;
                }
                case OpCode::CONST_F64:
                    registers[i.dst] = blueprint.getConstants()[i.auxiliary];
                    break;
                case OpCode::ADD_F64:
                    registers[i.dst]  = registers[i.src0] + registers[i.src1];
                    break;
                case OpCode::SUB_F64:
                    registers[i.dst]  = registers[i.src0] - registers[i.src1];
                    break;
                case OpCode::MUL_F64:
                    registers[i.dst]  = registers[i.src0] * registers[i.src1];
                    break;
                case OpCode::DIV_F64:
                    registers[i.dst]  = registers[i.src0] / registers[i.src1];
                    break;
                case OpCode::STORE_OUTPUT_F64:
                    outputs[i.auxiliary] = registers[i.src0];
                    break;
                default:
                    throw std::logic_error("invalide operation type");
                    break;
            }
        }

        return outputs;
    }
}
