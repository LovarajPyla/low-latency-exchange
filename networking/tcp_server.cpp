// TCP order-entry gateway.
//
// Network thread:
//   TCP + epoll/kqueue -> SPSC queue
//
// Matching thread:
//   SPSC queue -> RiskEngine -> OrderBook
//   OrderBook -> outbound response queue
//
// The web demo can therefore use the real C++ matching engine.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/epoll.h>
#else
#include <sys/event.h>
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../concurrency/spsc_queue.hpp"
#include "../engine/order_book.hpp"
#include "../risk/risk_engine.hpp"
#include "protocol.hpp"

namespace {

uint64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        perror("fcntl(F_GETFL)");
        return;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
    }
}

struct GatewayEvent {
    MsgType type;
    uint64_t order_id;
    uint8_t side;
    int64_t price;
    uint32_t quantity;
    uint64_t timestamp_ns;
    int client_fd;
};

struct OutboundMessage {
    int client_fd;
    std::vector<uint8_t> bytes;
};

constexpr int MAX_EVENTS = 64;
constexpr int PORT = 9000;
constexpr size_t QUEUE_CAPACITY = 1 << 16;

} // namespace

int main() {

    // Prevent a disconnected client from terminating the gateway
    // when send() encounters a broken socket.
    std::signal(SIGPIPE, SIG_IGN);

    SpscQueue<GatewayEvent> queue(QUEUE_CAPACITY);

    std::atomic<bool> running{true};

    // Matching thread produces responses.
    // Network thread consumes them.
    std::mutex outboundMutex;
    std::deque<OutboundMessage> outbound;

    // ============================================================
    // Matching-engine thread
    // ============================================================

    std::thread matchingThread([&] {

        OrderBook book;
        RiskEngine risk;

        // Associates each active/pending order with its TCP client.
        std::unordered_map<uint64_t, int> orderOwners;

        auto enqueueResponse =
            [&](int fd, const void* data, size_t size) {

                if (fd < 0) {
                    return;
                }

                OutboundMessage message;
                message.client_fd = fd;
                message.bytes.resize(size);

                std::memcpy(
                    message.bytes.data(),
                    data,
                    size
                );

                std::lock_guard<std::mutex> lock(outboundMutex);

                outbound.push_back(
                    std::move(message)
                );
            };

        // ------------------------------------------------------------
        // Trade callback
        // ------------------------------------------------------------

        book.setTradeCallback(
            [&](const Trade& trade) {

                TradeMsg message{};

                message.type =
                    static_cast<uint8_t>(MsgType::TRADE);

                message.buy_order_id =
                    trade.buy_order_id;

                message.sell_order_id =
                    trade.sell_order_id;

                message.price =
                    trade.price;

                message.quantity =
                    trade.quantity;

                auto buyIt =
                    orderOwners.find(trade.buy_order_id);

                auto sellIt =
                    orderOwners.find(trade.sell_order_id);

                // Send trade to buyer's client.
                if (buyIt != orderOwners.end()) {

                    enqueueResponse(
                        buyIt->second,
                        &message,
                        sizeof(message)
                    );
                }

                // Send trade to seller's client.
                if (sellIt != orderOwners.end() &&
                    (buyIt == orderOwners.end() ||
                     sellIt->second != buyIt->second)) {

                    enqueueResponse(
                        sellIt->second,
                        &message,
                        sizeof(message)
                    );
                }

                std::cout
                    << "TRADE"
                    << " buy=" << trade.buy_order_id
                    << " sell=" << trade.sell_order_id
                    << " price=" << trade.price
                    << " qty=" << trade.quantity
                    << '\n';
            }
        );

        GatewayEvent event{};

        while (
            running.load(
                std::memory_order_relaxed
            )
        ) {

            if (!queue.pop(event)) {

                // Keep the matching loop simple.
                std::this_thread::yield();

                continue;
            }

            // ========================================================
            // NEW ORDER
            // ========================================================

            if (event.type == MsgType::NEW_ORDER) {

                const Side side =
                    event.side == 0
                        ? Side::BUY
                        : Side::SELL;

                // Risk validation.
                const RejectReason reason =
                    risk.check(
                        event.order_id,
                        side,
                        event.price,
                        event.quantity
                    );

                if (reason != RejectReason::NONE) {

                    RejectMsg reject{};

                    reject.type =
                        static_cast<uint8_t>(
                            MsgType::REJECT
                        );

                    reject.order_id =
                        event.order_id;

                    reject.reason_code =
                        static_cast<uint8_t>(reason);

                    enqueueResponse(
                        event.client_fd,
                        &reject,
                        sizeof(reject)
                    );

                    std::cerr
                        << "REJECT"
                        << " order="
                        << event.order_id
                        << " reason="
                        << static_cast<int>(reason)
                        << '\n';

                    continue;
                }

                // Register owner before matching.
                // This is important because a new order can
                // immediately generate a trade.
                orderOwners[event.order_id] =
                    event.client_fd;

                const bool accepted =
                    book.addOrder(
                        event.order_id,
                        side,
                        event.price,
                        event.quantity,
                        event.timestamp_ns
                    );

                if (!accepted) {

                    orderOwners.erase(
                        event.order_id
                    );

                    RejectMsg reject{};

                    reject.type =
                        static_cast<uint8_t>(
                            MsgType::REJECT
                        );

                    reject.order_id =
                        event.order_id;

                    reject.reason_code =
                        static_cast<uint8_t>(
                            RejectReason::DUPLICATE_ORDER
                        );

                    enqueueResponse(
                        event.client_fd,
                        &reject,
                        sizeof(reject)
                    );

                    continue;
                }

                risk.recordAccepted(
                    event.order_id
                );

                // Tell the client that the order
                // was accepted by the C++ engine.
                AckMsg ack{};

                ack.type =
                    static_cast<uint8_t>(
                        MsgType::ACK
                    );

                ack.order_id =
                    event.order_id;

                enqueueResponse(
                    event.client_fd,
                    &ack,
                    sizeof(ack)
                );
            }

            // ========================================================
            // CANCEL ORDER
            // ========================================================

            else if (
                event.type ==
                MsgType::CANCEL_ORDER
            ) {

                const bool cancelled =
                    book.cancelOrder(
                        event.order_id
                    );

                if (cancelled) {

                    AckMsg ack{};

                    ack.type =
                        static_cast<uint8_t>(
                            MsgType::ACK
                        );

                    ack.order_id =
                        event.order_id;

                    enqueueResponse(
                        event.client_fd,
                        &ack,
                        sizeof(ack)
                    );

                    orderOwners.erase(
                        event.order_id
                    );
                }
                else {

                    RejectMsg reject{};

                    reject.type =
                        static_cast<uint8_t>(
                            MsgType::REJECT
                        );

                    reject.order_id =
                        event.order_id;

                    reject.reason_code = 0;

                    enqueueResponse(
                        event.client_fd,
                        &reject,
                        sizeof(reject)
                    );
                }
            }
        }
    });

    // ============================================================
    // TCP listening socket
    // ============================================================

    int listenFd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (listenFd < 0) {

        perror("socket");

        running.store(false);
        matchingThread.join();

        return 1;
    }

    int option = 1;

    if (
        setsockopt(
            listenFd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &option,
            sizeof(option)
        ) < 0
    ) {

        perror("setsockopt");
    }

    sockaddr_in address{};

    address.sin_family =
        AF_INET;

    address.sin_addr.s_addr =
        INADDR_ANY;

    address.sin_port =
        htons(PORT);

    if (
        bind(
            listenFd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)
        ) < 0
    ) {

        perror("bind");

        close(listenFd);

        running.store(false);
        matchingThread.join();

        return 1;
    }

    if (
        listen(
            listenFd,
            SOMAXCONN
        ) < 0
    ) {

        perror("listen");

        close(listenFd);

        running.store(false);
        matchingThread.join();

        return 1;
    }

    setNonBlocking(listenFd);

    // Per-client receive buffers.
    std::unordered_map<
        int,
        std::vector<uint8_t>
    > receiveBuffers;

#ifdef __linux__

    // ============================================================
    // Linux epoll
    // ============================================================

    int eventFd =
        epoll_create1(0);

    if (eventFd < 0) {

        perror("epoll_create1");

        close(listenFd);

        running.store(false);
        matchingThread.join();

        return 1;
    }

    epoll_event listenEvent{};

    listenEvent.events =
        EPOLLIN;

    listenEvent.data.fd =
        listenFd;

    if (
        epoll_ctl(
            eventFd,
            EPOLL_CTL_ADD,
            listenFd,
            &listenEvent
        ) < 0
    ) {

        perror("epoll_ctl");

        close(eventFd);
        close(listenFd);

        running.store(false);
        matchingThread.join();

        return 1;
    }

    std::vector<epoll_event>
        events(MAX_EVENTS);

    std::cout
        << "Gateway listening on port "
        << PORT
        << " (epoll, non-blocking)"
        << '\n';

#else

    // ============================================================
    // macOS kqueue
    // ============================================================

    int eventFd =
        kqueue();

    if (eventFd < 0) {

        perror("kqueue");

        close(listenFd);

        running.store(false);
        matchingThread.join();

        return 1;
    }

    struct kevent listenEvent{};

    EV_SET(
        &listenEvent,
        listenFd,
        EVFILT_READ,
        EV_ADD | EV_ENABLE,
        0,
        0,
        nullptr
    );

    if (
        kevent(
            eventFd,
            &listenEvent,
            1,
            nullptr,
            0,
            nullptr
        ) < 0
    ) {

        perror("kevent");

        close(eventFd);
        close(listenFd);

        running.store(false);
        matchingThread.join();

        return 1;
    }

    std::vector<struct kevent>
        events(MAX_EVENTS);

    std::cout
        << "Gateway listening on port "
        << PORT
        << " (kqueue, non-blocking)"
        << '\n';

#endif

    // ============================================================
    // Close client
    // ============================================================

    auto closeClient =
        [&](int fd) {

#ifdef __linux__

            epoll_ctl(
                eventFd,
                EPOLL_CTL_DEL,
                fd,
                nullptr
            );

#else

            struct kevent deleteEvent{};

            EV_SET(
                &deleteEvent,
                fd,
                EVFILT_READ,
                EV_DELETE,
                0,
                0,
                nullptr
            );

            kevent(
                eventFd,
                &deleteEvent,
                1,
                nullptr,
                0,
                nullptr
            );

#endif

            close(fd);

            receiveBuffers.erase(fd);

            std::cout
                << "Client disconnected fd="
                << fd
                << '\n';
        };

    // ============================================================
    // Send pending responses
    // ============================================================

    auto flushOutbound =
        [&] {

            std::deque<OutboundMessage>
                pending;

            {
                std::lock_guard<std::mutex>
                    lock(outboundMutex);

                pending.swap(outbound);
            }

            while (!pending.empty()) {

                OutboundMessage message =
                    std::move(
                        pending.front()
                    );

                pending.pop_front();

                if (
                    !receiveBuffers.contains(
                        message.client_fd
                    )
                ) {
                    continue;
                }

                size_t sent = 0;

                while (
                    sent <
                    message.bytes.size()
                ) {

                    ssize_t n =
                        send(
                            message.client_fd,
                            message.bytes.data() + sent,
                            message.bytes.size() - sent,
                            0
                        );

                    if (n > 0) {

                        sent +=
                            static_cast<size_t>(n);

                        continue;
                    }

                    if (
                        n < 0 &&
                        (
                            errno == EAGAIN ||
                            errno == EWOULDBLOCK
                        )
                    ) {

                        OutboundMessage remaining;

                        remaining.client_fd =
                            message.client_fd;

                        remaining.bytes.assign(
                            message.bytes.begin() + sent,
                            message.bytes.end()
                        );

                        std::lock_guard<
                            std::mutex
                        > lock(outboundMutex);

                        outbound.push_front(
                            std::move(remaining)
                        );

                        break;
                    }

                    closeClient(
                        message.client_fd
                    );

                    break;
                }
            }
        };

    // ============================================================
    // Network event loop
    // ============================================================

    while (
        running.load(
            std::memory_order_relaxed
        )
    ) {

        flushOutbound();

#ifdef __linux__

        int eventCount =
            epoll_wait(
                eventFd,
                events.data(),
                MAX_EVENTS,
                50
            );

#else

        struct timespec timeout{};

        timeout.tv_sec = 0;
        timeout.tv_nsec = 50'000'000;

        int eventCount =
            kevent(
                eventFd,
                nullptr,
                0,
                events.data(),
                MAX_EVENTS,
                &timeout
            );

#endif

        if (eventCount < 0) {

            if (errno == EINTR) {
                continue;
            }

#ifdef __linux__
            perror("epoll_wait");
#else
            perror("kevent");
#endif

            break;
        }

        for (
            int i = 0;
            i < eventCount;
            ++i
        ) {

#ifdef __linux__

            int fd =
                events[i].data.fd;

#else

            int fd =
                static_cast<int>(
                    events[i].ident
                );

#endif

            // ========================================================
            // New connection
            // ========================================================

            if (fd == listenFd) {

                while (true) {

                    sockaddr_in client{};
                    socklen_t length =
                        sizeof(client);

                    int clientFd =
                        accept(
                            listenFd,
                            reinterpret_cast<sockaddr*>(
                                &client
                            ),
                            &length
                        );

                    if (clientFd < 0) {

                        if (
                            errno == EAGAIN ||
                            errno == EWOULDBLOCK
                        ) {
                            break;
                        }

                        perror("accept");

                        break;
                    }

                    setNonBlocking(
                        clientFd
                    );

#ifdef __linux__

                    epoll_event clientEvent{};

                    clientEvent.events =
                        EPOLLIN;

                    clientEvent.data.fd =
                        clientFd;

                    if (
                        epoll_ctl(
                            eventFd,
                            EPOLL_CTL_ADD,
                            clientFd,
                            &clientEvent
                        ) < 0
                    ) {

                        perror(
                            "epoll_ctl client"
                        );

                        close(clientFd);

                        continue;
                    }

#else

                    struct kevent clientEvent{};

                    EV_SET(
                        &clientEvent,
                        clientFd,
                        EVFILT_READ,
                        EV_ADD | EV_ENABLE,
                        0,
                        0,
                        nullptr
                    );

                    if (
                        kevent(
                            eventFd,
                            &clientEvent,
                            1,
                            nullptr,
                            0,
                            nullptr
                        ) < 0
                    ) {

                        perror(
                            "kevent client"
                        );

                        close(clientFd);

                        continue;
                    }

#endif

                    receiveBuffers[
                        clientFd
                    ] = {};

                    std::cout
                        << "Client connected fd="
                        << clientFd
                        << '\n';
                }

                continue;
            }

            // ========================================================
            // Existing client
            // ========================================================

            auto bufferIt =
                receiveBuffers.find(fd);

            if (
                bufferIt ==
                receiveBuffers.end()
            ) {
                continue;
            }

            auto& buffer =
                bufferIt->second;

            uint8_t readBuffer[4096];

            while (true) {

                ssize_t bytesRead =
                    recv(
                        fd,
                        readBuffer,
                        sizeof(readBuffer),
                        0
                    );

                // ----------------------------------------------------
                // Disconnected
                // ----------------------------------------------------

                if (bytesRead == 0) {

                    closeClient(fd);

                    break;
                }

                // ----------------------------------------------------
                // Error
                // ----------------------------------------------------

                if (bytesRead < 0) {

                    if (
                        errno == EAGAIN ||
                        errno == EWOULDBLOCK
                    ) {
                        break;
                    }

                    perror("recv");

                    closeClient(fd);

                    break;
                }

                buffer.insert(
                    buffer.end(),
                    readBuffer,
                    readBuffer + bytesRead
                );

                size_t offset = 0;

                // ====================================================
                // Frame messages
                // ====================================================

                while (
                    offset <
                    buffer.size()
                ) {

                    const MsgType type =
                        static_cast<MsgType>(
                            buffer[offset]
                        );

                    // ------------------------------------------------
                    // NEW_ORDER
                    // ------------------------------------------------

                    if (
                        type ==
                        MsgType::NEW_ORDER
                    ) {

                        if (
                            buffer.size() - offset <
                            sizeof(NewOrderMsg)
                        ) {
                            break;
                        }

                        NewOrderMsg message{};

                        std::memcpy(
                            &message,
                            buffer.data() + offset,
                            sizeof(message)
                        );

                        offset +=
                            sizeof(message);

                        GatewayEvent event{
                            MsgType::NEW_ORDER,
                            message.order_id,
                            message.side,
                            message.price,
                            message.quantity,
                            nowNs(),
                            fd
                        };

                        if (
                            !queue.push(event)
                        ) {

                            std::cerr
                                << "Queue full, "
                                << "dropping order "
                                << message.order_id
                                << '\n';
                        }
                    }

                    // ------------------------------------------------
                    // CANCEL_ORDER
                    // ------------------------------------------------

                    else if (
                        type ==
                        MsgType::CANCEL_ORDER
                    ) {

                        if (
                            buffer.size() - offset <
                            sizeof(CancelOrderMsg)
                        ) {
                            break;
                        }

                        CancelOrderMsg message{};

                        std::memcpy(
                            &message,
                            buffer.data() + offset,
                            sizeof(message)
                        );

                        offset +=
                            sizeof(message);

                        GatewayEvent event{
                            MsgType::CANCEL_ORDER,
                            message.order_id,
                            0,
                            0,
                            0,
                            nowNs(),
                            fd
                        };

                        if (
                            !queue.push(event)
                        ) {

                            std::cerr
                                << "Queue full, "
                                << "dropping cancel "
                                << message.order_id
                                << '\n';
                        }
                    }

                    // ------------------------------------------------
                    // HEARTBEAT
                    // ------------------------------------------------

                    else if (
                        type ==
                        MsgType::HEARTBEAT
                    ) {

                        if (
                            buffer.size() - offset <
                            sizeof(HeartbeatMsg)
                        ) {
                            break;
                        }

                        HeartbeatMsg message{};

                        std::memcpy(
                            &message,
                            buffer.data() + offset,
                            sizeof(message)
                        );

                        offset +=
                            sizeof(message);

                        AckMsg ack{};

                        ack.type =
                            static_cast<uint8_t>(
                                MsgType::ACK
                            );

                        ack.order_id =
                            message.seq_num;

                        std::lock_guard<
                            std::mutex
                        > lock(outboundMutex);

                        OutboundMessage response;

                        response.client_fd =
                            fd;

                        response.bytes.resize(
                            sizeof(ack)
                        );

                        std::memcpy(
                            response.bytes.data(),
                            &ack,
                            sizeof(ack)
                        );

                        outbound.push_back(
                            std::move(response)
                        );
                    }

                    // ------------------------------------------------
                    // Unknown message
                    // ------------------------------------------------

                    else {

                        std::cerr
                            << "Malformed message on fd="
                            << fd
                            << ", dropping connection"
                            << '\n';

                        offset =
                            buffer.size();

                        closeClient(fd);

                        break;
                    }
                }

                if (
                    receiveBuffers.find(fd) ==
                    receiveBuffers.end()
                ) {
                    break;
                }

                buffer.erase(
                    buffer.begin(),
                    buffer.begin() +
                        static_cast<long>(offset)
                );
            }
        }
    }

    // ============================================================
    // Shutdown
    // ============================================================

    running.store(false);

    matchingThread.join();

    close(eventFd);
    close(listenFd);

    return 0;
}