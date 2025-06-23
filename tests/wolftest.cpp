#include <iostream>
#include <string_view>
#include <thread>
#include <atomic>

#include <wolf.h>

static void on_listen(wolf::TcpListenerView listener,
                      wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::puts("on listen network error");
    } else {
        std::puts("started listening");
    }
}

static void on_read(wolf::TcpClientView client, std::uint8_t *buf, std::size_t size, void *context, wolf::NetworkError err) {
    static std::atomic<int> count = 0;
    if (err != wolf::NetworkError::Ok) {
        std::cout << "on read error " << ++count << '\n';
    } else {
        std::cout << "on read called got data: " << std::string_view((char*)buf, size) << ' ' << ++count << '\n';
    }
}

static void on_write(wolf::TcpClientView client, void *context, wolf::NetworkError err) {
    
}

static void on_accept(wolf::TcpClientView client,
                      wolf::NetworkError err) {
    static int count = 0;
    static const char data[] = "HELLO WORLD";
    if (err != wolf::NetworkError::Ok) {
        std::puts("on accept network error");
    } else {
        std::cout << "accepted connection " << ++count << '\n';
        client.set_onread(on_read);
        client.set_onwrite(on_write);
        client.write((std::uint8_t*)data, sizeof(data)-1);
    }
}

static void on_connect(wolf::TcpClientView client, void *context,
                      wolf::NetworkError err) {
    static const char data[] = "FROM CONNECT";
    if (err != wolf::NetworkError::Ok) {
        std::puts("on connect error");
    } else {
        std::puts("successful on connect");
        client.set_onread(on_read);
        client.set_onwrite(on_write);
        client.write((std::uint8_t*)data, sizeof(data)-1);
    }
}

int main(void) {
    wolf::EventLoop loop;
    // use raw address for now until async getaddrinfo is added.
    loop.tcp_listen(wolf::ipv4_address(127, 0, 0, 1), 4444, on_listen,
                    on_accept);
                    
    loop.tcp_connect(wolf::ipv4_address(127, 0, 0, 2), 4444, nullptr, on_connect);

    for (int i = 0; i < 11; i++) {
        std::thread([]{
            wolf::EventLoop loop;
            // use raw address for now until async getaddrinfo is added.
            loop.tcp_listen(wolf::ipv4_address(127, 0, 0, 1), 4444, on_listen,
                    on_accept);
            loop.run();
        }).detach();
    }

    loop.run();

    return 0;
}
