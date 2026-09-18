#include "faststreamcompute/program.hpp"
#include "faststreamcompute/record.hpp"

#include <vector>
#include <string>


namespace faststreamcompute {
    enum class OpCode {
        NOP,
        LOAD_F64,
        CONST_F64,
        ADD_F64, SUB_F64, MUL_F64, DIV_F64,
        STORE_OUTPUT_F64
    };

    typedef size_t RegisterID;

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
            int register_count = 0;

        public:
            ExecutionBlueprint(const Program& p);

            const std::vector<Instruction>& getInstructions() const;
            const std::vector<double>& getConstants() const; 
            const std::vector<std::string>& getOutputNames() const;
            const int& getRegisterCount() const;
    };

    class BytecodeExecutor {
        private:
            ExecutionBlueprint blueprint;
            std::vector<double> registers;
            std::vector<double> outputs;

        public:
            BytecodeExecutor(ExecutionBlueprint bp);

            const std::vector<double>& execute(const QuoteRecord& record);
    };
}
