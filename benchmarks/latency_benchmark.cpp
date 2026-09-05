// Latency + throughput benchmark for the OrderBook.
//
// Measures wall-clock time from addOrder() call to return for each order,
// which covers parsing-equivalent overhead, matching, and trade emission
// (everything inside the hot path except real network I/O). Writes a CSV
// of per-order latencies so python/analyze_latency.py can compute and plot
// percentiles.
//
// IMPORTANT: run this yourself and report your own numbers. Do not copy
// numbers from this file's comments -- there are none, intentionally.
//
// Build: g++ -std=c++20 -O3 -march=native -DNDEBUG -I.. latency_benchmark.cpp -o bench
// Run:   ./bench 1000000 latency_results.csv

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

#include "../engine/order_book.hpp"

int main(int argc, char** argv) {
    uint64_t numOrders = (argc > 1) ? std::stoull(argv[1]) : 1'000'000ULL;
    std::string outPath = (argc > 2) ? argv[2] : "latency_results.csv";

    OrderBook book;
    std::vector<double> latenciesUs;
    latenciesUs.reserve(numOrders);

    std::mt19937_64 rng(7);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> qtyDist(1, 100);
    std::uniform_int_distribution<int> priceOffset(-25, 25);

    int64_t mid = 10000;
    auto benchStart = std::chrono::high_resolution_clock::now();

    for (uint64_t i = 0; i < numOrders; ++i) {
        Side side = sideDist(rng) == 0 ? Side::BUY : Side::SELL;
        int64_t price = mid + priceOffset(rng);
        uint32_t qty = static_cast<uint32_t>(qtyDist(rng));

        auto t0 = std::chrono::high_resolution_clock::now();
        book.addOrder(i, side, price, qty);
        auto t1 = std::chrono::high_resolution_clock::now();

        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        latenciesUs.push_back(us);
    }

    auto benchEnd = std::chrono::high_resolution_clock::now();
    double totalSeconds = std::chrono::duration<double>(benchEnd - benchStart).count();

    std::vector<double> sorted = latenciesUs;
    std::sort(sorted.begin(), sorted.end());
    auto pct = [&](double p) {
        size_t idx = static_cast<size_t>(p / 100.0 * (sorted.size() - 1));
        return sorted[idx];
    };

    std::cout << "\n========== LATENCY BENCHMARK ==========\n"
              << "Orders:        " << numOrders << '\n'
              << "Trades:        " << book.tradeCount() << '\n'
              << "Total time:    " << totalSeconds << " s\n"
              << "Throughput:    " << (numOrders / totalSeconds) << " orders/sec\n"
              << "p50 latency:   " << pct(50) << " us\n"
              << "p90 latency:   " << pct(90) << " us\n"
              << "p95 latency:   " << pct(95) << " us\n"
              << "p99 latency:   " << pct(99) << " us\n"
              << "p99.9 latency: " << pct(99.9) << " us\n"
              << "max latency:   " << sorted.back() << " us\n"
              << "========================================\n"
              << "NOTE: these numbers are only meaningful on the machine\n"
              << "they were measured on. Record CPU model, compiler, flags\n"
              << "and OS alongside any number you put on a resume.\n";

    std::ofstream csv(outPath);
    csv << "order_index,latency_us\n";
    for (size_t i = 0; i < latenciesUs.size(); ++i) {
        csv << i << ',' << latenciesUs[i] << '\n';
    }
    std::cout << "Wrote per-order latencies to " << outPath << '\n';
    return 0;
}
