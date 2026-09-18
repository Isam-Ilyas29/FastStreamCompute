#pragma once

#include "faststreamcompute/program.hpp"
#include "faststreamcompute/record.hpp"

#include <unordered_map>


namespace faststreamcompute {
    
    // Reference Backend
    std::unordered_map<std::string, double> executor(const QuoteRecord& record, const Program& program);
}
