#include "files.h"
#include <cstring>
#include <iostream>

#include <span>
#include <string_view>
#include <wolf.h>

static void on_listen(wolf::TcpListenerView listener, wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::puts("on_listen: error");
    } else {
        std::puts("on_listen: success");
    }
}

static void on_recv(wolf::TcpClientView client, std::span<uint8_t> buf, void *context,
                    wolf::NetworkError err) {
    if (err != wolf::NetworkError::Ok) {
        std::cout << "on_recv: error\n";
        return;
    }

    std::string_view sv{(char*)buf.data(), buf.size()};
    std::cout << "got: " << sv << std::endl;

    static const char data[] = "HELLO WORLD";
    client.send((uint8_t*)data, sizeof(data) - 1, (void *)0x12452);
}

static void on_send(wolf::TcpClientView client, std::span<const uint8_t> buf, void *context,
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
    if (err != wolf::NetworkError::Ok) {
        std::puts("on_accept: error");
        return;
    }

    client.set_onrecv(on_recv);
    client.set_onsend(on_send);
    client.set_onclose(on_close);
}

static uint8_t READ_BUFFER[1024];

static void on_read(wolf::FileView file, void *ctx, std::span<uint8_t> buf, uint64_t off, uint64_t tok, wolf::FileError err) {
    if (err != wolf::FileError::Ok) {
        std::cout << "read error\n";
    }

    std::string_view sv{(char*)buf.data(), buf.size()};
    std::cout << "file contents: " << sv << "size: " << buf.size() << '\n';
}

int main(void) {
    wolf::EventLoop loop;
    // use raw address for now until async getaddrinfo is added.
    loop.tcp_listen(wolf::ipv4_address(127, 0, 0, 1), 4444, on_listen, on_accept);

    wolf::Handle handle =
        loop.set_timeout([](void *context) { std::puts("timeout"); }, nullptr, 1000);

    loop.file_open(
        "./fuck.txt", wolf::FileOpenMode::Read, wolf::FileOpenOptions::Create, 0666, nullptr,
        [](wolf::FileView file, void *context, wolf::FileError err) {
            std::puts("file open");
            file.set_onread(on_read);
            file.read_from(0, std::span{(uint8_t*)READ_BUFFER, sizeof(READ_BUFFER)});
        }
    );

    loop.run();

    return 0;
}
