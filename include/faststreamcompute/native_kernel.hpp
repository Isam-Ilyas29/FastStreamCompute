#pragma once

#include "faststreamcompute/record.hpp"

#include <span>


namespace faststreamcompute {
    // Do not return a newly allocated vector because allocation would become part of execution
    void midpointAOS(std::span<const QuoteRecord> input, std::span<double> output);
    void spreadAOS(std::span<const QuoteRecord> input, std::span<double> output);
    void relativeSpreadAOS(std::span<const QuoteRecord> input, std::span<double> output);
}
