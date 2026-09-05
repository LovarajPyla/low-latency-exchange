// Standalone demo/benchmark entry point for the OrderBook.
// Build: g++ -std=c++20 -O3 -march=native -DNDEBUG -I.. matching_engine.cpp -o matching_engine

#include <chrono>
#include <iostream>

#include "order_book.hpp"

int main() {
    OrderBook book;

    // Price is represented using integer ticks, e.g. 100.25 USD -> 10025.
    book.addOrder(1, Side::SELL, 10050, 100);
    book.addOrder(2, Side::SELL, 10100, 200);
    book.addOrder(3, Side::BUY, 9900, 150);
    book.addOrder(4, Side::BUY, 9950, 100);
    book.printBook();

    std::cout << "\nAdding BUY 150 @ 10050\n";
    book.addOrder(5, Side::BUY, 10050, 150);
    book.printBook();

    std::cout << "\nAdding SELL 75 @ 9950\n";
    book.addOrder(6, Side::SELL, 9950, 75);
    book.printBook();

    std::cout << "\nCancelling order #2\n";
    if (book.cancelOrder(2)) std::cout << "Order cancelled successfully\n";
    book.printBook();

    // Simple throughput benchmark. For percentile latency, use
    // benchmarks/latency_benchmark.cpp instead.
    constexpr uint64_t NUM_ORDERS = 1'000'000;
    auto start = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 100000; i < 100000 + NUM_ORDERS; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        int64_t price = (side == Side::BUY) ? 9000 : 11000;
        book.addOrder(i, side, price, 1);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double seconds = elapsed.count() / 1'000'000.0;
    double throughput = NUM_ORDERS / seconds;

    std::cout << "\n========== BENCHMARK ==========\n"
              << "Orders: " << NUM_ORDERS << '\n'
              << "Time: " << elapsed.count() << " us\n"
              << "Throughput: " << throughput << " orders/sec\n"
              << "Trades: " << book.tradeCount() << '\n'
              << "================================\n";
    return 0;
}
