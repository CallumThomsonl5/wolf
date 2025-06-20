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

const char data[] = "HELLO WORLD";

static void on_accept(wolf::EventLoop &loop, wolf::TcpClientView client,
                      wolf::NetworkError err) {
    static int count = 0;
    if (err != wolf::NetworkError::Ok) {
        std::puts("on accept network error");
    } else {
        std::cout << "accepted connection " << ++count << '\n';
        client.write((std::uint8_t*)data, sizeof(data)-1);
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

static void on_write(wolf::EventLoop &loop, wolf::TcpClientView client, void *cookie, void *context, wolf::NetworkError err) {
    
}

int main(void) {
    wolf::EventLoop loop;
    // use raw address for now until async getaddrinfo is added.
    loop.tcp_listen(wolf::ipv4_address(127, 0, 0, 1), 4444, on_listen,
                    on_accept, on_read, on_write, nullptr);

    loop.run();

    return 0;
}
