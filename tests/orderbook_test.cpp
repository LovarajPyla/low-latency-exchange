// Unit tests for OrderBook, using GoogleTest.
//
// Build (after installing GoogleTest, e.g. `apt install libgtest-dev` or
// via CMake's FetchContent -- see CMakeLists.txt):
//   g++ -std=c++20 -I.. orderbook_test.cpp -lgtest -lgtest_main -pthread -o orderbook_test
// Run:
//   ./orderbook_test

#include <gtest/gtest.h>

#include "../engine/order_book.hpp"
#include "../risk/risk_engine.hpp"

TEST(OrderBook, SimpleCrossProducesTrade) {
    OrderBook book;
    book.addOrder(1, Side::SELL, 100, 50);
    book.addOrder(2, Side::BUY, 100, 50);
    ASSERT_EQ(book.tradeCount(), 1u);
    EXPECT_EQ(book.trades()[0].quantity, 50u);
    EXPECT_EQ(book.trades()[0].price, 100);
}

TEST(OrderBook, PartialFillLeavesRemainderOnBook) {
    OrderBook book;
    book.addOrder(1, Side::SELL, 100, 50);
    book.addOrder(2, Side::BUY, 100, 150); // 150 buy vs 50 sell -> 50 filled, 100 remain
    ASSERT_EQ(book.tradeCount(), 1u);
    EXPECT_EQ(book.trades()[0].quantity, 50u);

    int64_t price; uint32_t qty;
    ASSERT_TRUE(book.bestBid(price, qty));
    EXPECT_EQ(price, 100);
    EXPECT_EQ(qty, 100u);
}

TEST(OrderBook, NoCrossWhenPricesDoNotOverlap) {
    OrderBook book;
    book.addOrder(1, Side::SELL, 105, 50);
    book.addOrder(2, Side::BUY, 100, 50);
    EXPECT_EQ(book.tradeCount(), 0u);
}

TEST(OrderBook, CancelRemovesRestingOrder) {
    OrderBook book;
    book.addOrder(1, Side::SELL, 100, 50);
    ASSERT_TRUE(book.cancelOrder(1));
    book.addOrder(2, Side::BUY, 100, 50); // should not match the cancelled order
    EXPECT_EQ(book.tradeCount(), 0u);
}

TEST(OrderBook, CancelTwiceReturnsFalseSecondTime) {
    OrderBook book;
    book.addOrder(1, Side::SELL, 100, 50);
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_FALSE(book.cancelOrder(1));
}

TEST(OrderBook, CancelAfterFullFillReturnsFalse) {
    OrderBook book;
    book.addOrder(1, Side::SELL, 100, 50);
    book.addOrder(2, Side::BUY, 100, 50); // fully fills order 1
    EXPECT_FALSE(book.cancelOrder(1));    // already removed by the match
}

TEST(OrderBook, CancelNonexistentOrderReturnsFalse) {
    OrderBook book;
    EXPECT_FALSE(book.cancelOrder(999));
}

TEST(OrderBook, MultipleOrdersSamePriceRespectFifo) {
    OrderBook book;
    book.addOrder(1, Side::SELL, 100, 30); // resting first
    book.addOrder(2, Side::SELL, 100, 30); // resting second
    book.addOrder(3, Side::BUY, 100, 30);  // should match order 1, not order 2
    ASSERT_EQ(book.tradeCount(), 1u);
    EXPECT_EQ(book.trades()[0].sell_order_id, 1u);
}

TEST(OrderBook, DuplicateOrderIdRejected) {
    OrderBook book;
    EXPECT_TRUE(book.addOrder(1, Side::BUY, 100, 10));
    EXPECT_FALSE(book.addOrder(1, Side::BUY, 100, 10)); // duplicate id
}

TEST(OrderBook, ZeroQuantityRejected) {
    OrderBook book;
    EXPECT_FALSE(book.addOrder(1, Side::BUY, 100, 0));
}

TEST(RiskEngine, RejectsOversizedOrder) {
    RiskLimits limits;
    limits.max_order_qty = 100;
    RiskEngine risk(limits);
    EXPECT_EQ(risk.check(1, Side::BUY, 100, 200), RejectReason::MAX_ORDER_SIZE);
}

TEST(RiskEngine, RejectsDuplicateOrderId) {
    RiskEngine risk;
    EXPECT_EQ(risk.check(1, Side::BUY, 100, 10), RejectReason::NONE);
    risk.recordAccepted(1);
    EXPECT_EQ(risk.check(1, Side::BUY, 100, 10), RejectReason::DUPLICATE_ORDER);
}

TEST(RiskEngine, RejectsWhenPositionLimitExceeded) {
    RiskLimits limits;
    limits.max_position = 50;
    RiskEngine risk(limits);
    EXPECT_EQ(risk.check(1, Side::BUY, 100, 60), RejectReason::MAX_POSITION);
}

TEST(RiskEngine, RejectsMaxNotional) {
    RiskLimits limits;
    limits.max_notional = 1000;
    RiskEngine risk(limits);

    EXPECT_EQ(
        risk.check(1, Side::BUY, 200, 10),
        RejectReason::MAX_NOTIONAL
    );
}

TEST(RiskEngine, RejectsPriceDeviation) {
    RiskLimits limits;
    limits.reference_price = 100;
    limits.max_price_deviation_pct = 10.0;
    RiskEngine risk(limits);

    EXPECT_EQ(
        risk.check(1, Side::BUY, 120, 10),
        RejectReason::PRICE_DEVIATION
    );
}

TEST(RiskEngine, RejectsDailyLossLimit) {
    RiskLimits limits;
    limits.reference_price = 100;
    limits.max_daily_loss = -50;

    RiskEngine risk(limits);

    // SELL at 200 with reference price 100 creates
    // realized P&L of -100 when filled.
    risk.recordFill(Side::BUY, 200, 1);

    EXPECT_EQ(
        risk.check(1, Side::BUY, 100, 1),
        RejectReason::DAILY_LOSS_LIMIT
    );
}
