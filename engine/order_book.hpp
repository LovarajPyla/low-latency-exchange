#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "order.hpp"

// OrderBook: price-time priority limit order book + matching engine.
//
// Notes on data-structure choices (see docs/architecture.md):
//  - std::map gives ordered price levels "for free" (O(log n) insert,
//    O(1) best-price access via begin()). It is NOT the fastest possible
//    choice for the hot path (see docs/architecture.md optimization notes)
//    but it is correct, simple, and a reasonable MVP baseline.
//  - std::deque (not std::vector) is used for FIFO queues at each price
//    level so that pop-front is O(1) instead of O(n) (vector::erase(begin())
//    is O(n) because it shifts every remaining element).
//  - Cancellation is O(1) average via the orders map; physical removal
//    from the price-level deque is deferred (lazy deletion) and swept out
//    when the order reaches the front of the queue.
class OrderBook {
public:
    using TradeCallback = std::function<void(const Trade&)>;

    void setTradeCallback(TradeCallback cb) { onTrade_ = std::move(cb); }

    // Returns true if the order was accepted (even if fully filled).
    bool addOrder(uint64_t id, Side side, int64_t price, uint32_t quantity,
                   uint64_t timestamp_ns = 0) {
        if (quantity == 0) return false;
        if (orders_.contains(id)) {
            std::cerr << "Duplicate order ID: " << id << '\n';
            return false;
        }

        auto order = std::make_unique<Order>(id, side, price, quantity, timestamp_ns);
        Order* ptr = order.get();
        storage_.emplace(id, std::move(order));
        orders_.emplace(id, ptr);

        match(ptr);

        if (ptr->remaining > 0) {
            if (side == Side::BUY) {
                bids_[price].push_back(ptr);
            } else {
                asks_[price].push_back(ptr);
            }
        }
        return true;
    }

    bool cancelOrder(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        it->second->remaining = 0; // lazy delete; swept from queue on match/print
        orders_.erase(it);
        return true;
    }

    // Best bid / ask, returns false if book side is empty.
    bool bestBid(int64_t& price, uint32_t& qty) const {
        for (const auto& [p, q] : bids_) {
            uint32_t total = sumQty(q);
            if (total > 0) { price = p; qty = total; return true; }
        }
        return false;
    }
    bool bestAsk(int64_t& price, uint32_t& qty) const {
        for (const auto& [p, q] : asks_) {
            uint32_t total = sumQty(q);
            if (total > 0) { price = p; qty = total; return true; }
        }
        return false;
    }

    size_t tradeCount() const { return tradeCount_; }
    const std::vector<Trade>& trades() const { return trades_; }

    void printBook() const {
        std::cout << "\n========== ORDER BOOK ==========\n\nASKS\n";
        for (auto it = asks_.rbegin(); it != asks_.rend(); ++it) {
            uint32_t total = sumQty(it->second);
            if (total > 0) std::cout << "Price: " << it->first << " Quantity: " << total << '\n';
        }
        std::cout << "\nBIDS\n";
        for (const auto& [price, queue] : bids_) {
            uint32_t total = sumQty(queue);
            if (total > 0) std::cout << "Price: " << price << " Quantity: " << total << '\n';
        }
        std::cout << "\n================================\n";
    }

private:
    using BidBook = std::map<int64_t, std::deque<Order*>, std::greater<int64_t>>;
    using AskBook = std::map<int64_t, std::deque<Order*>, std::less<int64_t>>;

    BidBook bids_;
    AskBook asks_;
    std::unordered_map<uint64_t, Order*> orders_;                    // active orders
    std::unordered_map<uint64_t, std::unique_ptr<Order>> storage_;   // ownership
    std::vector<Trade> trades_;
    size_t tradeCount_ = 0;
    TradeCallback onTrade_;

    static uint32_t sumQty(const std::deque<Order*>& q) {
        uint32_t total = 0;
        for (Order* o : q) total += o->remaining;
        return total;
    }

    void match(Order* incoming) {
        if (incoming->side == Side::BUY) matchBuy(incoming);
        else matchSell(incoming);
    }

    void emit(const Trade& t) {
        trades_.push_back(t);
        ++tradeCount_;
        if (onTrade_) onTrade_(t);
    }

    void matchBuy(Order* buy) {
        while (buy->remaining > 0 && !asks_.empty()) {
            auto bestAsk = asks_.begin();
            if (buy->price < bestAsk->first) break;
            auto& queue = bestAsk->second;
            while (!queue.empty() && buy->remaining > 0) {
                Order* sell = queue.front();
                if (sell->remaining == 0) { queue.pop_front(); continue; }
                uint32_t qty = std::min(buy->remaining, sell->remaining);
                emit(Trade{buy->id, sell->id, sell->price, qty, buy->timestamp_ns});
                buy->remaining -= qty;
                sell->remaining -= qty;
                if (sell->remaining == 0) { orders_.erase(sell->id); queue.pop_front(); }
            }
            if (queue.empty()) asks_.erase(bestAsk);
        }
    }

    void matchSell(Order* sell) {
        while (sell->remaining > 0 && !bids_.empty()) {
            auto bestBid = bids_.begin();
            if (sell->price > bestBid->first) break;
            auto& queue = bestBid->second;
            while (!queue.empty() && sell->remaining > 0) {
                Order* buy = queue.front();
                if (buy->remaining == 0) { queue.pop_front(); continue; }
                uint32_t qty = std::min(sell->remaining, buy->remaining);
                emit(Trade{buy->id, sell->id, buy->price, qty, sell->timestamp_ns});
                sell->remaining -= qty;
                buy->remaining -= qty;
                if (buy->remaining == 0) { orders_.erase(buy->id); queue.pop_front(); }
            }
            if (queue.empty()) bids_.erase(bestBid);
        }
    }
};
