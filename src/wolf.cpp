#include <asm-generic/socket.h>
#include <cerrno>
#include <format>
#include <iostream>
#include <optional>

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
    fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd_ < 0) {
        // handle error
        std::cout << "socket err\n";
    }

    int val = 1;
    int err = setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (void*)&val, sizeof(val));
    if (err != 0) {
        // TODO handle error
    }

    struct addrinfo *addrinfo;
    err = getaddrinfo(host.c_str(), std::to_string(port).c_str(), NULL,
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

/// Closes the socket
TcpListener::~TcpListener() {
    if (close(fd_) < 0) {
        // handle error
    }
}

std::optional<int> TcpListener::accept() {
    int client = accept4(fd_, NULL, NULL, SOCK_NONBLOCK);
    if (client < 0) {
        // check if blocking
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return {};

        // TODO handle error
    }
    return client;
}

void TcpListener::closeClient(int client) {
    if (close(client) < 0) {
        // TODO handle error
    }
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

/// Adds a client to a watchlist
/// Allocates a Ctx struct which is managed internally
/// data is managed by the caller
void EventLoop::watch(int client, TcpListener &listener, void *data) {
    Ctx *ctx = new Ctx(listener, client, false, data);

    struct epoll_event ev;
    ev.data.ptr = ctx;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    if (epoll_ctl(epollfd_, EPOLL_CTL_ADD, client, &ev) < 0) {
        // TODO handle error     
    }
}

void EventLoop::unwatch(int client) {
    if (epoll_ctl(epollfd_, EPOLL_CTL_DEL, client, nullptr) < 0) {
        // TODO handle error
    }
}

void EventLoop::pollIO(int timeout) {
    struct epoll_event epoll_events[1024];
    int nfds = epoll_wait(epollfd_, epoll_events, 1024, timeout);

    for (epoll_event *ev = epoll_events; ev < epoll_events + nfds; ev++) {
        Ctx *ctx = (Ctx *)ev->data.ptr;

        if (ev->events & EPOLLRDHUP) {
            unwatch(ctx->fd);
            ctx->listener.closeClient(ctx->fd);
            delete ctx;
            std::cout << "holdup\n";
        } else if (ev->events & EPOLLIN) {
            if (ctx->is_listener) {
                ctx->listener.on_connect(*this, ctx->listener);
            } else {
                ctx->listener.on_readable(*this, *ctx);
            }
            std::cout << "readable\n";
        } else if (ev->events & EPOLLOUT) {
            ctx->listener.on_writeable(*this, *ctx);
            std::cout << "writeable\n";
        } else {
            std::cout << "epoll unknown event\n";
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
