#include <iostream>

#include <wolf.h>

int main(void) {
    wolf::EventLoop loop(12);
    // use raw address for now until async getaddrinfo is added.
    wolf::NetworkError err = loop.tcp_listen(wolf::ipv4_address(127, 0, 0, 1), 4444,
                      [] { std::cout << "on_accept for 127.0.0.1 called\n"; });
    if (err != wolf::NetworkError::Ok) {
        puts("Failed to create listening socket");
        return 1;
    }
    loop.run();

    return 0;
}
