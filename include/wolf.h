#ifndef WOLF_H_INCLUDED
#define WOLF_H_INCLUDED

#include <cstdint>
#include <list>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <functional>
#include <vector>
#include <string>

namespace wolf {

const int WOLF_MAJOR_VERSION = 0;
const int WOLF_MINOR_VERSION = 0;
const int WOLF_PATCH_VERSION = 1;

const int CLIENT_BUF_SIZE = 256;

class TcpClient {
public:
    TcpClient(int fd, struct sockaddr sockaddr): fd_(fd), sockaddr_(sockaddr) {}
    int send(char *buf);
    int send(const std::string &str);
    int recv();
    int close();
private:
    int fd_;
    struct sockaddr sockaddr_;
};

class TcpListener {
public:
    TcpListener(std::string host, uint16_t port);
    void setOnAccept(std::function<void(TcpClient&)> on_accept) {
        on_accept_ = on_accept;
    }
    void onReadable(std::function<void()> on_readable);
    void onWriteable(std::function<void()> on_writeable);
    void onClose(std::function<void()> on_close);

    int getFD() {
        return fd_;
    }

    TcpClient &addClient(int fd, struct sockaddr sockaddr) {
        return clients_.emplace_back(fd, sockaddr);
    }

    inline void onAccept(TcpClient &client) {
        on_accept_(client);
    };
private:
    std::string host_;
    uint16_t port_;
    int fd_;
    std::function<void(TcpClient&)> on_accept_;
    std::list<TcpClient> clients_;
};

struct EpollData {
    EpollData(int fd__, TcpClient *client, bool is_listener__): fd(fd__) {
        ptr.client = client;
        is_listener = false;
    }
    EpollData(int fd__, TcpListener *listener, bool is_listener__): fd(fd__) {
        ptr.listener = listener;
        is_listener = true;
    }
    int fd;
    union {
        TcpClient *client;
        TcpListener *listener;
    } ptr;
    bool is_listener;
};

class EventLoop {
public:
    EventLoop();
    int run();
    void attatchListener(TcpListener &listener);
private:
    void pollIO(int timeout);

    bool is_running_ = false;
    int epollfd_ = -1;
    std::list<EpollData> epoll_data_;
};

void displayVersion();

} // namespace wolf

#endif // WOLF_H_INCLUDED
