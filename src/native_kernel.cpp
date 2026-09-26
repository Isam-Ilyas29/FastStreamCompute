#include "faststreamcompute/native_kernel.hpp"


#include <stdexcept>
#include <cstddef>

namespace faststreamcompute {
    void midpointAOS(std::span<const faststreamcompute::QuoteRecord> input, std::span<double> output) {
        if (input.size() != output.size()) {
            throw std::invalid_argument("midpoint size mismatch");
        }
        
        for (std::size_t i = 0; i < input.size(); ++i) {
            double sum = input[i].ask + input[i].bid;
            output[i] = sum * 0.5;
        }
    }

    void spreadAOS(std::span<const faststreamcompute::QuoteRecord> input, std::span<double> output) {
        if (input.size() != output.size()) {
            throw std::invalid_argument("spread size mismatch");
        }

        for (std::size_t i = 0; i < input.size(); ++i) {
            output[i] = input[i].ask - input[i].bid;
        }
    }

    void relativeSpreadAOS(std::span<const faststreamcompute::QuoteRecord> input, std::span<double> output) {
        if (input.size() != output.size()) {
            throw std::invalid_argument("relative spread size mismatch");
        }

        for (std::size_t i = 0; i < input.size(); ++i) {
            double spread = input[i].ask - input[i].bid;
            double sum = input[i].ask + input[i].bid;
            output[i] = spread / sum;
        }
    }
}
