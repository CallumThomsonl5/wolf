#include <string>
#include <iostream>

#include <wolf.h>

// testing
#include <sys/socket.h>
#include <unistd.h>

// fake http example
// enum class HttpResult { ERROR, INCOMPLETE, COMPLETE };

// HttpResult parse_http(const std::uint8_t *buf, size_t &offt) {
//     offt = 10;
//     return HttpResult::COMPLETE;
// }

// void on_recv(wolf::Client &client) {
//     size_t offt = 0;
//     HttpResult result = parse_http(client.recv_buf(), offt);
//     switch (result) {
//     case HttpResult::COMPLETE:
//     {
//         // std::string data = "HTTP/1.1 200 OK\r\nContent-Length: 19\r\n\r\ncall me if they die";
//         std::string data = "HTTP/1.1 200 OK\r\nContent-Length: ";
//         std::string content = "fuck you bitch cunt";
//         data.append(std::to_string(content.size()) + "\r\n\r\n");
//         data.append(content);
//         client.send(std::move(data));
//         // client.recv_buf_shift_down(offt);
//         break;
//     }
//     case HttpResult::ERROR:
//         client.close();
//         break;
//     default:
//         break;
//     }
// }

void on_connect(wolf::TcpListener &listener) {
    std::cout << "on_connect called\n";

    wolf::Client *client = listener.accept();
    if (!client) {
        return;
    }

    // client->recv(on_recv);
}

int main(void) {
    wolf::EventLoop loop;
    wolf::TcpListener listener("localhost", 4444, on_connect);
    
    loop.attach(listener);
    loop.run();


    return 0;
}