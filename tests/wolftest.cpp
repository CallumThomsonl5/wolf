#include <iostream>
#include <wolf.h>

int main() {
    wolf::displayVersion();

    wolf::EventLoop loop;

    wolf::TcpListener tcp_listener("localhost", 4444);
    tcp_listener.setOnAccept([](wolf::TcpClient &client) {
        std::string str("fuck you");
        client.send(str);
    });

    loop.attatchListener(tcp_listener);
    loop.run();


    std::cout << "hello world";

    return 0;
}
