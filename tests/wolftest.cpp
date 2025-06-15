#include <iostream>

#include <wolf.h>

static void on_listen(wolf::EventLoop &loop, wolf::TcpListenerView listener,
                      wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::puts("on listen network error");
    } else {
        std::puts("started listening");
    }
}

static void on_accept(wolf::EventLoop &loop, wolf::TcpClientView client,
                      wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::puts("on accept network error");
    } else {
        std::puts("accepted connection");
    }
}

int main(void) {
    wolf::EventLoop loop;
    // use raw address for now until async getaddrinfo is added.
    loop.tcp_listen(wolf::ipv4_address(127, 0, 0, 1), 4444, on_listen,
                    on_accept, nullptr, nullptr, nullptr);
    loop.run();

    return 0;
}
