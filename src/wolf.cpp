#include <cstdint>
#include <format>
#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <wolf.h>

namespace wolf {

// TCP
TcpListener::TcpListener(std::string host, uint16_t port)
    : host_(host), port_(port) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        // handle error
        std::cout << "socket err\n";
    }

    struct addrinfo *addrinfo;
    int err = getaddrinfo(host.c_str(), std::to_string(port).c_str(), NULL,
                          &addrinfo);
    if (err != 0) {
        // handle error
        std::cout << "getaddrinfo error\n";
    }

    while (addrinfo) {
        err = bind(fd_, addrinfo->ai_addr, sizeof(*addrinfo->ai_addr));
        if (err == 0)
            break;
        addrinfo = addrinfo->ai_next;
    }

    if (err < 0) {
        // handle err
        std::cout << "bind error\n";
    }

    err = listen(fd_, 200);
    if (err < 0) {
        // handle error
        std::cout << "listen error\n";
    }

    freeaddrinfo(addrinfo);
};

int TcpClient::send(const std::string &str) {
    std::cout << "sent " << str << std::endl;

    return 0;
}

// Event Loop
EventLoop::EventLoop() {
    epollfd_ = epoll_create1(0);
    if (epollfd_ < 0) {
        // handle error
    }
}

void EventLoop::attatchListener(TcpListener &listener) {
    struct epoll_event epoll_event;
    epoll_event.events = EPOLLIN | EPOLLOUT | EPOLLET;
    EpollData& epd = epoll_data_.emplace_back(listener.getFD(), &listener, true);
    epoll_event.data.ptr = &epd;
    int err =
        epoll_ctl(epollfd_, EPOLL_CTL_ADD, listener.getFD(), &epoll_event);
    if (err < 0) {
        // handle error
    }
}

void EventLoop::pollIO(int timeout) {
    struct epoll_event epoll_events[1024];
    int nfds = epoll_wait(epollfd_, epoll_events, 1024, timeout);
    if (nfds < 0) {
        // handle error
    }

    for (int i = 0; i < nfds; i++) {
        struct EpollData *epd = (struct EpollData *)epoll_events[i].data.ptr;

        // is a listener?
        if (epd->is_listener) {
            TcpListener *listener = epd->ptr.listener;

            // accept
            struct sockaddr sockaddr;
            socklen_t addrlen = 0;
            int fd = accept(epd->fd, &sockaddr, &addrlen);
            if (fd < 0) {
                // handle error
                std::cout << "accept error\n";
            }
            TcpClient &client = listener->addClient(fd, sockaddr);
            listener->onAccept(client);
        }
    }
}

int EventLoop::run() {
    is_running_ = true;
    int timeout = -1;

    // the event loop
    while (is_running_) {
        std::cout << "loop iter\n";
        pollIO(timeout);
    }

    return 0;
}

// Misc
void displayVersion() {
    std::cout << "Wolf Version "
              << std::format("{}.{}.{}", WOLF_MAJOR_VERSION, WOLF_MINOR_VERSION,
                             WOLF_PATCH_VERSION)
              << std::endl;
}

} // namespace wolf
