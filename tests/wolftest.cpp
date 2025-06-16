#include <iostream>
#include <string_view>
#include <thread>
#include <atomic>

#include <wolf.h>

static void on_listen(wolf::EventLoop &loop, wolf::TcpListenerView listener,
                      wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::puts("on listen network error");
    } else {
        std::puts("started listening");
    }
}

static void on_listen2(wolf::EventLoop &loop, wolf::TcpListenerView listener,
                      wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::puts("on listen network error 2");
    } else {
        std::puts("started listening 2");
    }
}

static void on_accept(wolf::EventLoop &loop, wolf::TcpClientView client,
                      wolf::NetworkError err) {
    static int count = 0;
    if (err != wolf::NetworkError::Ok) {
        std::puts("on accept network error");
    } else {
        std::cout << "accepted connection " << ++count << '\n';
    }
}

static void on_accept2(wolf::EventLoop &loop, wolf::TcpClientView client,
                      wolf::NetworkError err) {
    static int count = 0;
    if (err != wolf::NetworkError::Ok) {
        std::puts("on accept network error 2");
    } else {
        std::cout << "accepted connection 2 " << ++count << '\n';
    }
}

static void on_read(wolf::EventLoop &loop, wolf::TcpClientView client, std::uint8_t *buf, std::size_t size, wolf::NetworkError err) {
    static std::atomic<int> count = 0;
    if (err != wolf::NetworkError::Ok) {
        std::cout << "on read error " << ++count << '\n';
    } else {
        std::cout << "on read called got data: " << std::string_view((char*)buf, size) << ' ' << ++count << '\n';
    }
}

int main(void) {
    wolf::EventLoop loop;
    wolf::EventLoop loop2;
    // use raw address for now until async getaddrinfo is added.
    loop.tcp_listen(wolf::ipv4_address(127, 0, 0, 1), 4444, on_listen,
                    on_accept, on_read, nullptr, nullptr);

    std::thread([&]{
        loop2.tcp_listen(wolf::ipv4_address(127, 0, 0, 1), 4444, on_listen2,
                    on_accept2, on_read, nullptr, nullptr);
        loop2.run();
    }).detach();
    
    loop.run();

    return 0;
}
