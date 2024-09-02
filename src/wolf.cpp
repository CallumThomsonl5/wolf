#include <cerrno>
#include <cstring>
#include <format>
#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <list.h>
#include <unistd.h>
#include <wolf.h>

namespace wolf {

/// Client
Client::Client() {}

Client::Client(int fd, TcpListener *listener, EventLoop *loop)
    : fd_(fd), listener_(listener), loop_(loop) {}

Client::~Client() {}

/**
 * @brief Creates a listening socket
 *
 * Creates a socket listening on host:port. Does not start
 * accepting connections until attached to the event loop.
 * After being attached to the loop, when there is an
 * incomming connection, the callback on_conn is called.
 *
 * @throws ListenerException
 */
TcpListener::TcpListener(std::string host, std::uint16_t port,
                         on_connect_t on_conn)
    : host_(host), port_(port), on_conn_(on_conn) {
    fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd_ < 0) {
        std::string msg = "Failed to create socket, got error: ";
        msg += strerror(errno);
        throw ListenerException(msg);
    }

    int val = 1;
    int err =
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(val));
    if (err < 0) {
        std::string msg = "Failed to set socket option REUSEADDR, got error: ";
        msg += strerror(errno);
        throw ListenerException(msg);
    }

    struct addrinfo *addrinfo;
    struct addrinfo hints;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    err = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                      &addrinfo);
    if (err != 0) {
        std::string msg = "getaddrinfo failed with error: ";
        msg += gai_strerror(err);
        throw ListenerException(msg);
    }

    while (addrinfo) {
        err = bind(fd_, addrinfo->ai_addr, addrinfo->ai_addrlen);
        if (err == 0)
            break;
        addrinfo = addrinfo->ai_next;
    }

    if (err < 0) {
        std::string msg =
            std::format("Listener failed to bind to {}:{}. Got error {}", host,
                        port, strerror(errno));
        throw ListenerException(msg);
    }

    err = listen(fd_, 200);
    if (err < 0) {
        std::string msg = "Listener failed to listen, got error: ";
        msg += strerror(errno);
        throw ListenerException(msg);
    }

    freeaddrinfo(addrinfo);
}

/**
 * @brief Closes the associated file descriptor
 */
TcpListener::~TcpListener() { close(fd_); }

/**
 * @throws AcceptException
 */
Client *TcpListener::accept() {
    struct sockaddr_storage addr;
    socklen_t addr_len = 0;
    int fd = accept4(fd_, (struct sockaddr *)&addr, &addr_len, SOCK_NONBLOCK);
    if (fd < 0) {
        if (errno & (EAGAIN | EWOULDBLOCK)) {
            return nullptr;
        }
        std::string msg = "Failed to accept client, with error: ";
        msg += strerror(errno);
        throw AcceptException(msg);
        // TODO make this better
    }

    return loop->create_client(fd, this);
}

/*************************************************
 *                    Loop                        *
 *************************************************/

/**
 * @brief Creates a new event loop.
 * @throws EpollException
 */
EventLoop::EventLoop() {
    if ((epollfd_ = epoll_create1(0)) < 0) {
        std::string msg = "Epoll failed to create with error: ";
        msg += strerror(errno);
        throw EpollException(msg);
    }
}

/**
 * @brief Destroys the event loop.
 */
EventLoop::~EventLoop() {
    close(epollfd_);

    for (TcpListener &x : listeners_) {
        x.loop = nullptr;
    }
}

/**
 * @brief Runs the event loop until stopped.
 *
 * Blocks the thread, running in an infinite loop until
 * stopped by a call to stop(), or by an exception being thrown.
 */
void EventLoop::run() {
    is_running_ = true;
    while (is_running_) {
        poll_io(-1);
    }
}

/**
 * @brief Attach a listener to the event loop
 *
 * Attaches a listener to the event loop. The listener will start
 * receiving connect events from the loop. The @ref TcpListener::loop "loop"
 * field is set to this loop.
 *
 * @throws EpollException
 */
void EventLoop::attach(TcpListener &listener) {
    listener.loop = this;

    struct epoll_event ev;
    ev.data.ptr = &listener;
    ev.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epollfd_, EPOLL_CTL_ADD, listener.fd(), &ev)) {
        std::string msg = "Failed to add listener, got error: ";
        msg += strerror(errno);
        throw EpollException(msg);
    }

    listeners_.push_back(listener);
}

void EventLoop::poll_io(int timeout) {
    constexpr int MAX_EVENTS = 1024;
    struct epoll_event epoll_events[MAX_EVENTS];
    int nfds = epoll_wait(epollfd_, epoll_events, MAX_EVENTS, timeout);

    for (epoll_event *ev = epoll_events; ev < epoll_events + nfds; ev++) {
        wolf::EpollBase *base = static_cast<wolf::EpollBase *>(ev->data.ptr);
        if (wolf::Client *client = dynamic_cast<wolf::Client *>(base)) {

        } else {
            wolf::TcpListener *listener =
                static_cast<wolf::TcpListener *>(base);
            listener->on_conn();
        }

        // if (ev->events & EPOLLRDHUP) {
        //     unwatch(ctx->fd);
        //     ctx->listener.closeClient(ctx->fd);
        //     delete ctx;
        //     std::cout << "holdup\n";
        // } else if (ev->events & EPOLLIN) {
        //     if (ctx->is_listener) {
        //         ctx->listener.on_connect(*this, ctx->listener);
        //     } else {
        //         ctx->listener.on_readable(*this, *ctx);
        //     }
        //     std::cout << "readable\n";
        // } else if (ev->events & EPOLLOUT) {
        //     ctx->listener.on_writeable(*this, *ctx);
        //     std::cout << "writeable\n";
        // } else {
        //     std::cout << "epoll unknown event\n";
        // }
    }
}

} // namespace wolf
