#pragma once
#include <cstdint>
#include <cstring>

// Fixed-width binary wire protocol.
// Every message is a 1-byte type tag followed by a fixed-size payload,
// so the feed handler can parse with no delimiter scanning.
//
//   [ uint8_t type ][ payload ... ]
//
// This keeps parsing O(1) per message and avoids string parsing on the
// critical path (JSON/text parsing is far too slow for a hot path).

#pragma pack(push, 1)

enum class MsgType : uint8_t {
    NEW_ORDER = 1,
    CANCEL_ORDER = 2,
    HEARTBEAT = 3,
    // Server -> client
    ACK = 10,
    REJECT = 11,
    TRADE = 12,
    BOOK_UPDATE = 13,
};

struct NewOrderMsg {
    uint8_t type = static_cast<uint8_t>(MsgType::NEW_ORDER);
    uint64_t seq_num;
    uint64_t order_id;
    uint8_t side;      // 0 = BUY, 1 = SELL
    int64_t price;     // integer ticks
    uint32_t quantity;
};

struct CancelOrderMsg {
    uint8_t type = static_cast<uint8_t>(MsgType::CANCEL_ORDER);
    uint64_t seq_num;
    uint64_t order_id;
};

struct HeartbeatMsg {
    uint8_t type = static_cast<uint8_t>(MsgType::HEARTBEAT);
    uint64_t seq_num;
    uint64_t send_time_ns;
};

struct AckMsg {
    uint8_t type = static_cast<uint8_t>(MsgType::ACK);
    uint64_t order_id;
};

struct RejectMsg {
    uint8_t type = static_cast<uint8_t>(MsgType::REJECT);
    uint64_t order_id;
    uint8_t reason_code; // see risk/risk_engine.hpp RejectReason
};

struct TradeMsg {
    uint8_t type = static_cast<uint8_t>(MsgType::TRADE);
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    int64_t price;
    uint32_t quantity;
};

#pragma pack(pop)

constexpr size_t MAX_MSG_SIZE = sizeof(NewOrderMsg); // largest client message
