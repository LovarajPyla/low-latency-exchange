#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "../engine/order.hpp"

enum class RejectReason : uint8_t {
    NONE = 0,
    MAX_ORDER_SIZE = 1,
    MAX_POSITION = 2,
    MAX_NOTIONAL = 3,
    PRICE_DEVIATION = 4,
    DAILY_LOSS_LIMIT = 5,
    DUPLICATE_ORDER = 6,
};

struct RiskLimits {
    uint32_t max_order_qty = 10'000;
    int64_t max_position = 100'000;        // net signed position, in shares
    int64_t max_notional = 50'000'000;     // price_ticks * qty
    int64_t reference_price = 0;           // last known "fair" price for deviation check
    double max_price_deviation_pct = 10.0; // % away from reference_price
    int64_t max_daily_loss = -5'000'000;   // realized P&L floor (negative)
};

// Deterministic, unit-testable pre-trade risk checks. Kept outside the
// matching engine's hot loop: the risk engine runs once per inbound order,
// before the order reaches the book.
class RiskEngine {
public:
    explicit RiskEngine(RiskLimits limits = {}) : limits_(limits) {}

    // Returns RejectReason::NONE if the order passes all checks.
    RejectReason check(uint64_t order_id, Side side, int64_t price, uint32_t quantity) {
        if (seenOrderIds_.contains(order_id)) return RejectReason::DUPLICATE_ORDER;

        if (quantity > limits_.max_order_qty) return RejectReason::MAX_ORDER_SIZE;

        int64_t signedQty = (side == Side::BUY) ? static_cast<int64_t>(quantity)
                                                  : -static_cast<int64_t>(quantity);
        int64_t projectedPosition = position_ + signedQty;
        if (projectedPosition > limits_.max_position ||
            projectedPosition < -limits_.max_position) {
            return RejectReason::MAX_POSITION;
        }

        int64_t notional = price * static_cast<int64_t>(quantity);
        if (notional > limits_.max_notional) return RejectReason::MAX_NOTIONAL;

        if (limits_.reference_price > 0) {
            double deviation =
                100.0 * std::abs(static_cast<double>(price - limits_.reference_price)) /
                static_cast<double>(limits_.reference_price);
            if (deviation > limits_.max_price_deviation_pct) {
                return RejectReason::PRICE_DEVIATION;
            }
        }

        if (realizedPnl_ < limits_.max_daily_loss) return RejectReason::DAILY_LOSS_LIMIT;

        return RejectReason::NONE;
    }

    // Call once an order has actually been accepted onto the book.
    void recordAccepted(uint64_t order_id) { seenOrderIds_.insert(order_id); }

    // Call on each trade fill to keep position/P&L current.
    void recordFill(Side side, int64_t price, uint32_t quantity) {
        int64_t signedQty = (side == Side::BUY) ? static_cast<int64_t>(quantity)
                                                  : -static_cast<int64_t>(quantity);
        position_ += signedQty;
        // Simplified mark-to-reference P&L; a real engine would track
        // per-lot cost basis. Sufficient for a deterministic risk gate.
        if (limits_.reference_price > 0) {
            realizedPnl_ -= signedQty * (price - limits_.reference_price);
        }
    }

    int64_t position() const { return position_; }
    int64_t realizedPnl() const { return realizedPnl_; }
    void setReferencePrice(int64_t p) { limits_.reference_price = p; }

private:
    RiskLimits limits_;
    int64_t position_ = 0;
    int64_t realizedPnl_ = 0;
    std::unordered_set<uint64_t> seenOrderIds_;
};
