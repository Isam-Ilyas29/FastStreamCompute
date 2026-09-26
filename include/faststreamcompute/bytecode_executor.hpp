#pragma once

#include "faststreamcompute/program.hpp"
#include "faststreamcompute/record.hpp"

#include <cstddef>
#include <string>
#include <vector>
#include <span>


namespace faststreamcompute {
    enum class OpCode {
        NOP,
        LOAD_F64,
        CONST_F64,
        ADD_F64, SUB_F64, MUL_F64, DIV_F64,
        STORE_OUTPUT_F64
    };

    using RegisterID = std::size_t;

    enum class InputField : std::size_t {
        Bid,
        Ask
    };

    struct Instruction {
        OpCode op;
        RegisterID dst;
        RegisterID src0; 
        RegisterID src1;
        std::size_t auxiliary;
    };

    class ExecutionBlueprint {
        private:
            std::vector<Instruction> instructions;
            std::vector<double> constants;
            std::vector<std::string> output_names;
            std::size_t register_count = 0;

        public:
            ExecutionBlueprint(const Program& p);

            const std::vector<Instruction>& getInstructions() const;
            const std::vector<double>& getConstants() const; 
            const std::vector<std::string>& getOutputNames() const;
            std::size_t getRegisterCount() const;
    };

    class BytecodeExecutor {
        private:
            ExecutionBlueprint blueprint;
            std::vector<double> registers;
            std::vector<double> outputs;

        public:
            BytecodeExecutor(ExecutionBlueprint bp);

            // The returned output reference is overwritten by the next execute() call
            const std::vector<double>& execute(const QuoteRecord& record);
            std::size_t outputCount() const noexcept;
    };

    class ChunkedBytecodeExecutor {
        private:
            ExecutionBlueprint blueprint;
            std::size_t chunk_capacity;
            std::vector<double> scratch;

        public:
            ChunkedBytecodeExecutor(ExecutionBlueprint bp, std::size_t capacity);
            void execute(const std::vector<QuoteRecord>& records, std::span<double> output);

    };

    // Single output for now
    void batchExecute(BytecodeExecutor& executor, std::span<const faststreamcompute::QuoteRecord> records, std::span<double> output);
}
