#include <format>
#include <iostream>
#include <string>
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
//         OK\r\nContent-Length: "; std::string content = "fuck you bitch cunt";
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
    std::cout << std::format("on_recv called. fd: {} bufsize: {}\n",
                             client.fd(), buffer.size());

    // for (auto x : buffer) {
    //     std::cout << (char)x;
    // }
    recv_count++;
    std::cout << "recv_count: " << recv_count << std::endl;
}

int connection_count = 0;
void on_connect(wolf::TcpListener &listener) {
    // std::cout << "on_connect called\n";
    while (wolf::Client *client = listener.accept()) {
        connection_count++;
        client->recv(on_recv);
    }

    std::cout << std::format("connection: {}", connection_count) << '\n';
}

int main(void) {
    wolf::EventLoop loop;
    wolf::TcpListener listener("localhost", 4444, on_connect);

    loop.attach(listener);
    loop.run();

    return 0;
}