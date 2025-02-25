#include <format>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <wolf.h>

// fake http example
enum class HttpResult { ERROR, INCOMPLETE, COMPLETE };

HttpResult parse_http(const std::vector<std::uint8_t> &buffer, size_t &offt) {
    offt = 10;
    return HttpResult::COMPLETE;
}

// void on_recv(wolf::Client &client, std::vector<std::uint8_t> buffer) {
//     size_t offt = 0;
//     HttpResult result = parse_http(buffer, offt);
//     switch (result) {
//     case HttpResult::COMPLETE:
//     {
//         // std::string data = "HTTP/1.1 200 OK\r\nContent-Length:
//         19\r\n\r\ncall me if they die"; std::string data = "HTTP/1.1 200
//         OK\r\nContent-Length: "; std::string content = "hello world";
//         data.append(std::to_string(content.size()) + "\r\n\r\n");
//         data.append(content);
//         client.send(data);
//         break;
//     }
//     case HttpResult::ERROR:
//         client.close();
//         break;
//     default:
//         break;
//     }
// }

int recv_count = 0;
void on_recv(wolf::Client &client, std::vector<std::uint8_t> buffer) {
    // std::cout << std::format("on_recv called. fd: {} bufsize: {}\n",
    //                          client.fd(), buffer.size());

    // for (auto x : buffer) {
    //     std::cout << (char)x;
    // }
    // recv_count++;
    // std::cout << "recv_count: " << recv_count << std::endl;
}

// int connection_count = 0;
void on_connect(wolf::TcpListener &listener) {
    // std::cout << "on_connect called\n";
    while (wolf::Client *client = listener.accept()) {
        // connection_count++;
        client->recv(on_recv);

        std::vector<std::uint8_t> buffer(16384, 'A');
        client->send(std::move(buffer));
    }

    // std::cout << std::format("connection: {}", connection_count) << '\n';
}

int main(void) {
    wolf::EventLoop loop;
    wolf::TcpListener listener("127.0.0.1", 4444, on_connect);
    wolf::TcpListener listener2("127.0.0.2", 4444, on_connect);
    wolf::TcpListener listener3("127.0.0.3", 4444, on_connect);
    wolf::TcpListener listener4("127.0.0.4", 4444, on_connect);
    wolf::TcpListener listener5("127.0.0.5", 4444, on_connect);
    wolf::TcpListener listener6("127.0.0.6", 4444, on_connect);

    loop.attach(listener);
    loop.attach(listener2);
    loop.attach(listener3);
    loop.attach(listener4);
    loop.attach(listener5);
    loop.attach(listener6);
    loop.run();

    return 0;
}
