#ifndef WOLF_H_INCLUDED
#define WOLF_H_INCLUDED

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace wolf {

const int WOLF_MAJOR_VERSION = 0;
const int WOLF_MINOR_VERSION = 0;
const int WOLF_PATCH_VERSION = 1;

// Forward declaration
class EventLoop;
class TcpListener;

struct Ctx {
    Ctx(TcpListener &listener__, int fd__, bool is_listener__, void *data__)
        : listener(listener__), fd(fd__), is_listener(is_listener__),
          data(data__) {}

    TcpListener &listener;
    int fd;
    bool is_listener;
    void *data;
};

class TcpListener {
public:
    TcpListener(std::string host, std::uint16_t port);
    ~TcpListener();

    int getFD() { return fd_; }
    std::optional<int> accept();
    void closeClient(int client);

    std::function<void(EventLoop &loop, TcpListener &listener)> on_connect;
    std::function<void(EventLoop &loop, Ctx &ctx)> on_readable;
    std::function<void(EventLoop &loop, Ctx &ctx)> on_writeable;

private:
    std::string host_;
    uint16_t port_;
    int fd_;
};

class EventLoop {
public:
    EventLoop();
    int run();
    void attatchListener(TcpListener &listener);

    void watch(int client, TcpListener &listener, void *data);
    void unwatch(int client);

private:
    void pollIO(int timeout);

    bool is_running_ = false;
    int epollfd_ = -1;
};

void displayVersion();

} // namespace wolf

#endif // WOLF_H_INCLUDED
