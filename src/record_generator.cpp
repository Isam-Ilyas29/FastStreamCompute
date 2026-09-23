#include "faststreamcompute/record_generator.hpp"

#include <random>


namespace faststreamcompute {
    [[nodiscard]] std::vector<QuoteRecord> generateRecords(std::size_t count) {
        std::vector<QuoteRecord> records;
        records.reserve(count);

        std::mt19937_64 e(1234);
        std::uniform_real_distribution bid_dist(0.0, 1000000.0);
        std::uniform_real_distribution spread_dist(1.0, 50.0);

        for (std::size_t i = 0; i < count; ++i) {
            double bid = bid_dist(e);
            double ask = bid + spread_dist(e);
            records.push_back({bid, ask});
        }

        return records;
    }
}
