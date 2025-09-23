#include <cstring>
#include <iostream>

#include <wolf.h>

static void on_listen(wolf::TcpListenerView listener, wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::puts("on_listen: error");
    } else {
        std::puts("on_listen: success");
    }
}

static void on_recv(wolf::TcpClientView client, std::uint8_t *buf, std::size_t size, void *context,
                    wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::cout << "on_recv: error\n";
        return;
    }

    client.send((std::uint8_t *)context, size, (void *)0x12452);
}

static void on_send(wolf::TcpClientView client, std::uint8_t *buf, std::size_t size, void *context,
                    void *send_ctx, wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::cout << "on_send: error\n";
        return;
    }
}

static void on_close(wolf::TcpClientView client, void *context, wolf::NetworkError err) {
    if (err == wolf::NetworkError::Ok) {
        free(context);
        return;
    }

    if (err == wolf::NetworkError::PeerShutdownWrite) {
        client.close();
        return;
    }

    std::puts("close error");
}

static void on_accept(wolf::TcpClientView client, wolf::NetworkError err) {
    static const char data[] = "HELLO WORLD";
    if (err != wolf::NetworkError::Ok) {
        std::puts("on_accept: error");
        return;
    }

    client.set_onrecv(on_recv);
    client.set_onsend(on_send);
    client.set_onclose(on_close);

    void *data2 = malloc(65536);
    client.set_context(data2);
}

int main(void) {
    wolf::EventLoop loop;
    // use raw address for now until async getaddrinfo is added.
    loop.tcp_listen(wolf::ipv4_address(127, 0, 0, 1), 4444, on_listen, on_accept);

    wolf::Handle handle =
        loop.set_timeout([](void *context) { std::puts("timeout"); }, nullptr, 1000);

    loop.run();

    return 0;
}
