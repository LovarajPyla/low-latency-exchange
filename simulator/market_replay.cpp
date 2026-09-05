// Market-data replay engine.
//
// Reads a binary event log (produced by generate_synthetic()) and feeds it
// through the OrderBook at a configurable speed multiplier, so the exact
// same workload can be replayed before/after an optimization for an
// apples-to-apples benchmark comparison.
//
// Usage:
//   ./replay --generate 100000 market_data.bin      # create a workload
//   ./replay --file market_data.bin --speed 10       # replay at 10x
//
// Build: g++ -std=c++20 -O3 -I.. market_replay.cpp -o replay

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "../engine/order_book.hpp"

#pragma pack(push, 1)
struct ReplayEvent {
    uint64_t timestamp_ns; // event time relative to start of file
    uint8_t type;          // 0 = NEW_ORDER, 1 = CANCEL
    uint64_t order_id;
    uint8_t side;           // 0 = BUY, 1 = SELL
    int64_t price;
    uint32_t quantity;
};
#pragma pack(pop)

// Generates a synthetic, reproducible order-flow file: a random walk around
// a mid-price with Poisson-ish inter-arrival gaps and a mix of new orders
// and cancels. Reproducible because it uses a fixed seed.
void generateSynthetic(const std::string& path, uint64_t numEvents) {
    std::ofstream out(path, std::ios::binary);
    std::mt19937_64 rng(42); // fixed seed => reproducible workload
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> qtyDist(1, 500);
    std::uniform_int_distribution<int> gapDist(100, 5000); // ns between events
    std::normal_distribution<double> walk(0.0, 3.0);
    std::uniform_real_distribution<double> cancelProb(0.0, 1.0);

    int64_t midPrice = 10000;
    uint64_t ts = 0;
    uint64_t nextId = 1;
    std::vector<uint64_t> liveIds;

    for (uint64_t i = 0; i < numEvents; ++i) {
        ts += gapDist(rng);
        midPrice += static_cast<int64_t>(walk(rng));
        if (midPrice < 100) midPrice = 100;

        ReplayEvent ev{};
        ev.timestamp_ns = ts;

        if (!liveIds.empty() && cancelProb(rng) < 0.1) {
            ev.type = 1; // CANCEL
            size_t idx = rng() % liveIds.size();
            ev.order_id = liveIds[idx];
            liveIds.erase(liveIds.begin() + idx);
        } else {
            ev.type = 0; // NEW_ORDER
            ev.order_id = nextId++;
            ev.side = static_cast<uint8_t>(sideDist(rng));
            int64_t offset = (ev.side == 0) ? -(rng() % 20) : (rng() % 20);
            ev.price = midPrice + offset;
            ev.quantity = static_cast<uint32_t>(qtyDist(rng));
            liveIds.push_back(ev.order_id);
        }
        out.write(reinterpret_cast<const char*>(&ev), sizeof(ev));
    }
    std::cout << "Generated " << numEvents << " events to " << path << '\n';
}

void replay(const std::string& path, double speed) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open " << path << '\n';
        return;
    }

    OrderBook book;
    size_t tradeCount = 0;
    book.setTradeCallback([&](const Trade&) { ++tradeCount; });

    ReplayEvent ev{};
    uint64_t eventsProcessed = 0;
    uint64_t lastTs = 0;
    auto wallStart = std::chrono::steady_clock::now();

    while (in.read(reinterpret_cast<char*>(&ev), sizeof(ev))) {
        if (speed > 0 && eventsProcessed > 0) {
            uint64_t deltaNs = static_cast<uint64_t>((ev.timestamp_ns - lastTs) / speed);
            if (deltaNs > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(deltaNs));
        }
        lastTs = ev.timestamp_ns;

        if (ev.type == 0) {
            Side side = ev.side == 0 ? Side::BUY : Side::SELL;
            book.addOrder(ev.order_id, side, ev.price, ev.quantity, ev.timestamp_ns);
        } else {
            book.cancelOrder(ev.order_id);
        }
        ++eventsProcessed;
    }

    auto wallEnd = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(wallEnd - wallStart).count();

    std::cout << "\n========== REPLAY SUMMARY ==========\n"
              << "Events processed: " << eventsProcessed << '\n'
              << "Trades generated: " << tradeCount << '\n'
              << "Wall time: " << seconds << " s\n"
              << "Effective throughput: " << (seconds > 0 ? eventsProcessed / seconds : 0)
              << " events/sec\n"
              << "=====================================\n";
}

int main(int argc, char** argv) {
    std::string file = "market_data.bin";
    double speed = 1.0;
    uint64_t generateCount = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--file" && i + 1 < argc) file = argv[++i];
        else if (arg == "--speed" && i + 1 < argc) speed = std::stod(argv[++i]);
        else if (arg == "--generate" && i + 1 < argc) {
            generateCount = std::stoull(argv[++i]);
            // Optional trailing positional path: --generate <count> <path>
            if (i + 1 < argc && argv[i + 1][0] != '-') file = argv[++i];
        }
    }

    if (generateCount > 0) {
        generateSynthetic(file, generateCount);
        return 0;
    }

    replay(file, speed);
    return 0;
}
