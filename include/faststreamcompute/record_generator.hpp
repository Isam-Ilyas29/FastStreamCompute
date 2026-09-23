#pragma once

#include "faststreamcompute/record.hpp"

#include <vector>


namespace faststreamcompute {
    [[nodiscard]] std::vector<QuoteRecord> generateRecords(std::size_t count);
}
