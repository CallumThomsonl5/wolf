#include <iostream>
#include <string_view>

#include <wolf.h>

static void on_listen(wolf::TcpListenerView listener,
                      wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::puts("on_listen: error");
    } else {
        std::puts("on_listen: success");
    }
}

static void on_recv(wolf::TcpClientView client, std::uint8_t *buf, std::size_t size, void *context, wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::cout << "on_recv: error\n";
        return;
    }
    
    std::cout << "on_read data: " << std::string_view((char*)buf, size) << '\n';
}

static void on_send(wolf::TcpClientView client, void *context, wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::cout << "on_send: error\n";
        return;
    }

    std::cout << "on_send: success\n";
}

static void on_close(wolf::TcpClientView client, void *context, wolf::NetworkError err) {
    if (err == wolf::NetworkError::Ok) {
        std::puts("on_close: full close");
        return;
    }

    if (err == wolf::NetworkError::PeerShutdownWrite) {
        std::puts("on_close: peer shutdown write, closing fully");
        client.close();
        return;
    }

    std::puts("close error");
}

static void on_accept(wolf::TcpClientView client,
                      wolf::NetworkError err) {
    static const char data[] = "HELLO WORLD";
    if (err != wolf::NetworkError::Ok) {
        std::puts("on_accept: error");
        return;
    }

    std::cout << "on_accept: accepted\n";
    client.set_onrecv(on_recv);
    client.set_onsend(on_send);
    client.set_onclose(on_close);
    client.send((std::uint8_t*)data, sizeof(data)-1);
}

int main(void) {
    wolf::EventLoop loop;
    // use raw address for now until async getaddrinfo is added.
    loop.tcp_listen(wolf::ipv4_address(127, 0, 0, 1), 4444, on_listen,
                    on_accept);
    
    loop.run();

    return 0;
}

