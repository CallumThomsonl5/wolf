#include <iostream>

#include <wolf.h>

int main(void) {
    wolf::EventLoop loop(12);
    // use raw address for now until async getaddrinfo is added.
    wolf::TcpListener listener(0x7F000001, 4444,
                               [] { std::cout << "on_accept called\n"; });
    loop.attach(listener);
    loop.run();

    return 0;
}
