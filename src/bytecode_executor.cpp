#include "faststreamcompute/bytecode_executor.hpp"

#include <algorithm>
#include <stdexcept>


namespace faststreamcompute {
    // ExecutionBlueprint

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
    std::size_t ExecutionBlueprint::getRegisterCount() const {
        return register_count;
    }


    // BytecodeExecutor

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

    std::size_t BytecodeExecutor::outputCount() const noexcept {
        return blueprint.getOutputNames().size();
    }


    // ChunkedBytecodeExecutor
    
    ChunkedBytecodeExecutor::ChunkedBytecodeExecutor(ExecutionBlueprint bp, std::size_t capacity) : blueprint(bp), chunk_capacity(capacity) {
        if (chunk_capacity == 0) {
            throw std::invalid_argument("chunk capacity must be greater than zero");
        }

        const std::size_t register_count = bp.getRegisterCount();

        if (register_count > scratch.max_size() / chunk_capacity) {
            throw std::length_error("scratch buffer is too large");
        }

        scratch.resize(chunk_capacity * register_count);
    }

    void ChunkedBytecodeExecutor::execute(const std::vector<QuoteRecord>& records, std::span<double> output) {
        if (records.size() != output.size()) {
            throw std::invalid_argument("batch size mismatch");
        }
        if (blueprint.getOutputNames().size() != 1) {
            throw std::invalid_argument("chunked execution requires exactly one output");
        }

        std::size_t offset = 0;
        while (offset < records.size()) {
            // The last chunk may contain fewer records than chunk_capacity.
            const std::size_t count = std::min(chunk_capacity, records.size() - offset);
            for (const Instruction& i: blueprint.getInstructions()) {
                switch (i.op) {
                    case OpCode::NOP:
                        // No Operation
                        break;
                    case OpCode::LOAD_F64: {
                        const InputField field = static_cast<InputField>(i.auxiliary);
                        if (field == InputField::Bid) {
                            for (std::size_t lane = 0; lane < count; ++lane) {
                                scratch[(i.dst * chunk_capacity) + lane] = records[offset + lane].bid;
                            }
                        }
                        else if (field == InputField::Ask) {
                            for (std::size_t lane = 0; lane < count; ++lane) {
                                scratch[(i.dst * chunk_capacity) + lane] = records[offset + lane].ask;
                            }
                        }
                        break;
                    }
                    case OpCode::CONST_F64:
                        for (std::size_t lane = 0; lane < count; ++lane) {
                            scratch[(i.dst * chunk_capacity) + lane] = blueprint.getConstants()[i.auxiliary];
                        }
                        break;
                    case OpCode::ADD_F64:
                        for (std::size_t lane = 0; lane < count; ++lane) {
                            scratch[(i.dst * chunk_capacity) + lane] = scratch[(i.src0 * chunk_capacity) + lane] + scratch[(i.src1 * chunk_capacity) + lane];
                        }
                        break;
                    case OpCode::SUB_F64:
                        for (std::size_t lane = 0; lane < count; ++lane) {
                            scratch[(i.dst * chunk_capacity) + lane] = scratch[(i.src0 * chunk_capacity) + lane] - scratch[(i.src1 * chunk_capacity) + lane];
                        }
                        break;
                    case OpCode::MUL_F64:
                        for (std::size_t lane = 0; lane < count; ++lane) {
                            scratch[(i.dst * chunk_capacity) + lane] = scratch[(i.src0 * chunk_capacity) + lane] * scratch[(i.src1 * chunk_capacity) + lane];
                        }
                        break;
                    case OpCode::DIV_F64:
                        for (std::size_t lane = 0; lane < count; ++lane) {
                            scratch[(i.dst * chunk_capacity) + lane] = scratch[(i.src0 * chunk_capacity) + lane] / scratch[(i.src1 * chunk_capacity) + lane];
                        }
                        break;
                    case OpCode::STORE_OUTPUT_F64:
                        for (std::size_t lane = 0; lane < count; ++lane) {
                            output[offset + lane] = scratch[(i.src0 * chunk_capacity) + lane];
                        }
                        break;
                    default:
                        throw std::logic_error("invalide operation type");
                        break;
                }
            }
            // Outputs are saved; reuse the same scratch rows for the next chunk.
            offset += count;
        }
    }


    //

    void batchExecute(BytecodeExecutor& executor, std::span<const faststreamcompute::QuoteRecord> records, std::span<double> output) {
        if (records.size() != output.size()) {
            throw std::invalid_argument("batch size mismatch");
        }
        if (executor.outputCount() != 1) {
            throw std::invalid_argument("batch requires exactly one output");
        }
        
        for (std::size_t i = 0; i < records.size(); ++i) {
            output[i] = executor.execute(records[i])[0];
        }
    }
}
