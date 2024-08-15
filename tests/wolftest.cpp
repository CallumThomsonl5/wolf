#include <iostream>
#include <optional>
#include <unistd.h>
#include <wolf.h>

#include <unistd.h>
#include <sys/socket.h>

struct UserCtx {
    int shit;
};

void onConnect(wolf::EventLoop &loop, wolf::TcpListener &listener) {
    std::cout << "listener with fd " << listener.getFD()
                  << " received connection" << std::endl;

    std::optional<int> client_opt = listener.accept();
    if (!client_opt.has_value()) return;
    int client = client_opt.value();

    std::cout << "accepted client with fd " << client << std::endl;

    UserCtx *user_ctx = new UserCtx();
    user_ctx->shit = 5;
    loop.watch(client, listener, user_ctx);
}

void onReadable(wolf::EventLoop &loop, wolf::Ctx &ctx) {
    std::uint8_t buf[1024] = {0};
    int n = recv(ctx.fd, buf, sizeof(buf) - 1, 0);
    std::cout << "got data from client: " << buf << "\n";
}

void onWriteable(wolf::EventLoop &loop, wolf::Ctx &ctx) {
    std::string hello{"hello world"};
    send(ctx.fd, hello.c_str(), hello.size(), 0);
}

int main() {
    wolf::displayVersion();

    wolf::EventLoop loop;

    wolf::TcpListener tcp_listener("localhost", 4444);
    tcp_listener.on_connect = onConnect;
    tcp_listener.on_readable = onReadable;
    tcp_listener.on_writeable = onWriteable;
    loop.attatchListener(tcp_listener);

    loop.run();

    std::cout << "hello world";

    return 0;
}
