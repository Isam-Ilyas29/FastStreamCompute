#include "faststreamcompute/program.hpp"
#include "faststreamcompute/record.hpp"
#include "faststreamcompute/reference_executor.hpp"
#include "faststreamcompute/bytecode_executor.hpp"
#include "faststreamcompute/native_kernel.hpp"
#include "faststreamcompute/record_generator.hpp"

#include <iostream>
#include <vector>
#include <unordered_map>


int main() {
    std::vector<faststreamcompute::QuoteRecord> records = faststreamcompute::generateRecords(1);

    // Midpoint -------------------------------------------------------------------
    
    // Program builder

    faststreamcompute::Program midpoint_p;
    auto bid1 = midpoint_p.inputf64("bid");
    auto ask1 = midpoint_p.inputf64("ask");
    auto midpoint = midpoint_p.mul(
        midpoint_p.add(bid1, ask1),
        midpoint_p.constant(0.5));
    midpoint_p.emit("midpoint", midpoint);

    // Reference executor
    
    std::cout << "Reference executor (midpoint): ";
    for (const auto& r: records ) {
        auto midpoint_reference_output = faststreamcompute::referenceExecutor(r, midpoint_p);
        
        for (const auto& [key, value] : midpoint_reference_output) {
            std::cout << value << ", ";
        }
    }

    // Bytecode executor

    faststreamcompute::ExecutionBlueprint midpoint_bp(midpoint_p);
    faststreamcompute::BytecodeExecutor midpoint_executor(midpoint_bp);

    std::cout << "\nBytecode executor (midpoint): ";
    for (const auto& r : records) {
        const auto& midpoint_bytecode_output = midpoint_executor.execute(r);
        for (const auto& o: midpoint_bytecode_output) {
            std::cout << o << ", ";
        }
    }

    // Kernel executor

    std::cout << "\nKernel executor (midpoint): ";
    std::vector<double> midpoint_kernel_output(records.size());
    faststreamcompute::midpointAOS(records, midpoint_kernel_output);

    for (const auto& o: midpoint_kernel_output) {
        std::cout << o << ", ";
    }

    // Spread --------------------------------------------------------------------

    // Program builder
    faststreamcompute::Program spread_p;
    auto bid2 = spread_p.inputf64("bid");
    auto ask2 = spread_p.inputf64("ask");
    auto spread1 = spread_p.sub(ask2, bid2);
    spread_p.emit("spread", spread1);

    // Reference executor
    
    std::cout << "\n\nReference executor (spread): ";
    for (const auto& r: records ) {
        auto spread_reference_output = faststreamcompute::referenceExecutor(r, spread_p);
        
        for (const auto& [key, value] : spread_reference_output) {
            std::cout << value << ", ";
        }
    }

    // Bytecode executor

    faststreamcompute::ExecutionBlueprint spread_bp(spread_p);
    faststreamcompute::BytecodeExecutor spread_executor(spread_bp);

    std::cout << "\nBytecode executor (spread): ";
    for (const auto& r : records) {
        const auto& spread_bytecode_output = spread_executor.execute(r);
        for (const auto& o: spread_bytecode_output) {
            std::cout << o << ", ";
        }
    }

    // Kernel executor

    std::cout << "\nKernel executor (spread): ";
    std::vector<double> spread_kernel_output(records.size());
    faststreamcompute::spreadAOS(records, spread_kernel_output);

    for (const auto& o: spread_kernel_output) {
        std::cout << o << ", ";
    }

    // Relative Spread -------------------------------------------------------------

    // Program builder

    faststreamcompute::Program relative_spread_p;
    auto bid3 = relative_spread_p.inputf64("bid");
    auto ask3 = relative_spread_p.inputf64("ask");
    auto sum = relative_spread_p.add(bid3, ask3);
    auto spread2 = relative_spread_p.sub(ask3, bid3);
    auto relative_spread = relative_spread_p.div(spread2, sum);
    relative_spread_p.emit("output", relative_spread);
    

    // Reference executor
    
    std::cout << "\n\nReference executor (relative spread): ";
    for (const auto& r: records ) {
        auto relative_spread_reference_output = faststreamcompute::referenceExecutor(r, relative_spread_p);
        
        for (const auto& [key, value] : relative_spread_reference_output) {
            std::cout << value << ", ";
        }
    }

    // Bytecode executor

    faststreamcompute::ExecutionBlueprint relative_spread_bp(relative_spread_p);
    faststreamcompute::BytecodeExecutor relative_spread_executor(relative_spread_bp);

    std::cout << "\nBytecode executor (relative spread): ";
    for (const auto& r : records) {
        const auto& relative_spread_bytecode_output = relative_spread_executor.execute(r);
        for (const auto& o: relative_spread_bytecode_output) {
            std::cout << o << ", ";
        }
    }

    // Kernel executor

    std::cout << "\nKernel executor (relative spread): ";
    std::vector<double> relative_spread_kernel_output(records.size());
    faststreamcompute::relativeSpreadAOS(records, relative_spread_kernel_output);

    for (const auto& o: relative_spread_kernel_output) {
        std::cout << o << ", ";
    }

    ////////////////////////////////////////////////////////////////////////

    // Batch test

    std::vector<double> outputs(records.size());
    faststreamcompute::batchExecute(relative_spread_executor, records, outputs);
    std::cout << "\n\n";
    for (const double& o: outputs) {
        std::cout << o << ", ";
    }
}
