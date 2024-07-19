#include <iostream>
#include <wolf.h>

struct UserCtx {
    int shit;
};

int main() {
    wolf::displayVersion();

    wolf::EventLoop loop;

    wolf::TcpListener tcp_listener("localhost", 4444);
    tcp_listener.on_connect = [](wolf::EventLoop &loop,
                                 wolf::TcpListener &listener) {
        std::cout << "listener with fd " << listener.getFD()
                  << " received connection" << std::endl;
        
        int client = listener.accept();
        // loop.watch(client, listener, data);
    };

    loop.attatchListener(tcp_listener);
    loop.run();

    std::cout << "hello world";

    return 0;
}
