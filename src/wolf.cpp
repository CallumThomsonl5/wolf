#include <format>
#include <iostream>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include <wolf.h>

namespace wolf {

/// TcpListener

/// Creates a socket and tries to start listening
/// The socket is marked as nonblocking
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

    // set nonblock
    int flags = fcntl(fd_, F_GETFL);
    if (flags < 0) {
        // handle error
        std::cout << "fcntl error\n";
    }

    err = fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    if (err < 0) {
        // handle error
        std::cout << "fcntl error\n";
    }
};

/// Closes the socket
TcpListener::~TcpListener() {
    if (close(fd_) < 0) {
        // handle error
    }
}

int TcpListener::accept() {
    return 0;
}

/// Event Loop

/// Initialise epoll
EventLoop::EventLoop() {
    epollfd_ = epoll_create1(0);
    if (epollfd_ < 0) {
        // handle error
    }
}

void EventLoop::attatchListener(TcpListener &listener) {
    struct epoll_event epoll_event;

    epoll_event.events = EPOLLIN | EPOLLOUT | EPOLLET;

    Ctx *ctx = new Ctx(listener, listener.getFD(), true, nullptr);
    epoll_event.data.ptr = ctx;

    int err =
        epoll_ctl(epollfd_, EPOLL_CTL_ADD, listener.getFD(), &epoll_event);
    if (err < 0) {
        // handle error
    }
}

void EventLoop::pollIO(int timeout) {
    struct epoll_event epoll_events[1024];
    int nfds = epoll_wait(epollfd_, epoll_events, 1024, timeout);

    for (int i = 0; i < nfds; i++) {
        struct epoll_event ev = epoll_events[i];
        Ctx *ctx = (Ctx *)ev.data.ptr;

        if (ev.events & EPOLLIN) {
            if (ctx->is_listener) {
                ctx->listener.on_connect(*this, ctx->listener);
            } else {
            }
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
