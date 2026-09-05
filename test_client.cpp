#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iostream>

#include "networking/protocol.hpp"

int main() {

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in server{};

    server.sin_family = AF_INET;
    server.sin_port = htons(9000);

    if (inet_pton(
            AF_INET,
            "127.0.0.1",
            &server.sin_addr) <= 0) {

        perror("inet_pton");
        close(sock);
        return 1;
    }

    if (connect(
            sock,
            reinterpret_cast<sockaddr*>(&server),
            sizeof(server)) < 0) {

        perror("connect");
        close(sock);
        return 1;
    }

    std::cout << "Connected to gateway\n";

    // ------------------------------------------------------------
    // Send BUY order
    // ------------------------------------------------------------

    NewOrderMsg buy{};

    buy.type =
        static_cast<uint8_t>(MsgType::NEW_ORDER);

    buy.seq_num = 1;
    buy.order_id = 1001;
    buy.side = 0;          // BUY
    buy.price = 10000;
    buy.quantity = 100;

    ssize_t sent =
        send(
            sock,
            &buy,
            sizeof(buy),
            0
        );

    if (sent != sizeof(buy)) {
        perror("send BUY");
    } else {
        std::cout
            << "Sent BUY"
            << " order_id=" << buy.order_id
            << " price=" << buy.price
            << " quantity=" << buy.quantity
            << '\n';
    }

    // ------------------------------------------------------------
    // Send SELL order that should match the BUY
    // ------------------------------------------------------------

    NewOrderMsg sell{};

    sell.type =
        static_cast<uint8_t>(MsgType::NEW_ORDER);

    sell.seq_num = 2;
    sell.order_id = 1002;
    sell.side = 1;         // SELL
    sell.price = 10000;
    sell.quantity = 50;

    sent =
        send(
            sock,
            &sell,
            sizeof(sell),
            0
        );

    if (sent != sizeof(sell)) {
        perror("send SELL");
    } else {
        std::cout
            << "Sent SELL"
            << " order_id=" << sell.order_id
            << " price=" << sell.price
            << " quantity=" << sell.quantity
            << '\n';
    }

    close(sock);

    std::cout << "Connection closed\n";

    return 0;
}
