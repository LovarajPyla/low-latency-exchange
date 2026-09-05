#pragma once
#include <cstdint>

enum class Side : uint8_t { BUY, SELL };

struct Order {
    uint64_t id;
    Side side;
    int64_t price;      // integer ticks
    uint32_t quantity;
    uint32_t remaining;
    uint64_t timestamp_ns; // for latency measurement / ordering

    Order() = default;
    Order(uint64_t id, Side side, int64_t price, uint32_t quantity, uint64_t ts = 0)
        : id(id), side(side), price(price), quantity(quantity),
          remaining(quantity), timestamp_ns(ts) {}
};

struct Trade {
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    int64_t price;
    uint32_t quantity;
    uint64_t timestamp_ns;
};
