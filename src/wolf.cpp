#include <algorithm>
#include <cassert>
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

#include <unistd.h>
#include <wolf.h>

namespace wolf {

constexpr std::size_t MAX_DATA = 65535;

const std::list<Client *>::iterator EMPTY_CLIENT_ITER;

Client::Client() {}

Client::Client(int fd, TcpListener *listener, EventLoop *loop)
    : fd_(fd), listener_(listener), loop_(loop) {}

Client::~Client() {}

void Client::recv(on_recv_t on_recv) { on_recv_ = on_recv; }

void Client::send(std::vector<std::uint8_t> vec) {
    send_buf_.push_back(std::move(vec));
    loop_->write_list_add(*this);
}

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
        close(fd_);
        std::string msg = "Failed to set socket option REUSEADDR, got error: ";
        msg += strerror(errno);
        throw ListenerException(msg);
    }

    struct addrinfo *addrinfo = nullptr;
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    err = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                      &addrinfo);
    if (err != 0) {
        close(fd_);
        std::string msg = "getaddrinfo failed with error: ";
        msg += gai_strerror(err);
        freeaddrinfo(addrinfo);
        throw ListenerException(msg);
    }

    while (addrinfo) {
        err = bind(fd_, addrinfo->ai_addr, addrinfo->ai_addrlen);
        if (err == 0)
            break;
        addrinfo = addrinfo->ai_next;
    }

    freeaddrinfo(addrinfo);

    if (err < 0) {
        close(fd_);
        std::string msg =
            std::format("Listener failed to bind to {}:{}. Got error {}", host,
                        port, strerror(errno));
        throw ListenerException(msg);
    }

    err = listen(fd_, 65535);
    if (err < 0) {
        close(fd_);
        std::string msg = "Listener failed to listen, got error: ";
        msg += strerror(errno);
        throw ListenerException(msg);
    }
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
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return nullptr;
        }
        std::string msg = "Failed to accept client, with error: ";
        msg += strerror(errno);
        throw AcceptException(msg);
        // TODO make this better
    }

    return &loop->create_client(fd, this);
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

    for (TcpListener *x : listeners_) {
        x->loop = nullptr;
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
    int timeout = -1;
    while (is_running_) {
        if (read_queue_.size() || write_queue_.size())
            timeout = 0;
        else
            timeout = -1;

        poll_io(timeout);
        run_read_events();
        run_write_events();
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

    listeners_.push_back(&listener);
}

/**
 * @brief Creates and stores a client object, registering it for events.
 */
Client &EventLoop::create_client(int fd, TcpListener *listener) {
    Client &client = clients_.emplace_back(fd, listener, this);
    client.loop_node_ = std::prev(clients_.end());
    read_list_add(client);

    // register in epoll
    // TODO
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLEXCLUSIVE | EPOLLET;
    ev.data.ptr = &client;
    epoll_ctl(epollfd_, EPOLL_CTL_ADD, client.fd(), &ev);

    return client;
}

/**
 * @brief Polls for IO, giving up after @param timeout seconds.
 */
void EventLoop::poll_io(int timeout) {
    constexpr int MAX_EVENTS = 1024;
    struct epoll_event epoll_events[MAX_EVENTS];
    int nfds = epoll_wait(epollfd_, epoll_events, MAX_EVENTS, timeout);

    for (epoll_event *ev = epoll_events; ev < epoll_events + nfds; ev++) {
        EpollBase *base = static_cast<EpollBase *>(ev->data.ptr);

        switch (base->get_type()) {
        case EpollBase::EpollType::Client: {
            Client *client = static_cast<Client *>(base);

            if (ev->events & EPOLLRDHUP) {
                throw EpollException("CONNECTION CLOSED NOT IMPLEMENTED");
            }

            if (ev->events & EPOLLIN) {
                // client becomes readable, add to read queue
                if (!in_read_list(*client)) {
                    read_list_add(*client);
                }
            }

            if (ev->events & EPOLLOUT) {
                if (client->send_buf_.size() && !in_write_list(*client)) {
                    write_list_add(*client);
                }
            }

            break;
        }
        case EpollBase::EpollType::Listener: {
            TcpListener *listener = static_cast<TcpListener *>(base);

            listener->on_conn();
            break;
        }
        }
    }
}

/**
 * @brief Attempts to read some data from each Client in the read list.
 *
 * If a client is not readable, it is removed from the list. This is also
 * where we detect client disconnection.
 */
void EventLoop::run_read_events() {
    auto it = read_queue_.begin();
    while (it != read_queue_.end()) {
        Client &client = *(*it);

        // read upto 65535 bytes
        std::vector<std::uint8_t> buffer(MAX_DATA);
        int n = recv(client.fd(), buffer.data(), MAX_DATA, 0);
        if (n == -1) {
            // check ewouldblock otherwise throw
            // if would block, notify
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                it = read_list_remove(it);
            } else {
                ++it;
                throw EpollException(
                    std::format("Read error: {}", strerror(errno)));
            }
        } else if (n == 0) {
            // client disconnected
            // TODO
            it = read_list_remove(it);
            // ++it;
            // throw EpollException("CLIENT DISCONNECTED NOT HANDLED");
        } else if (n < MAX_DATA) {
            // done read, remove and notify
            buffer.resize(n);
            client.on_recv_(client, std::move(buffer));
            it = read_list_remove(it);
        } else {
            // n == MAX_READ
            // more data avaible, keep in queue
            buffer.resize(n);
            client.on_recv_(client, std::move(buffer));
            ++it;
        }
    }
}

void EventLoop::run_write_events() {
    auto it = write_queue_.begin();
    while (it != write_queue_.end()) {
        Client &client = *(*it);

        bool remove_flag = false;
        std::size_t total = 0;
        while (total < MAX_DATA && !client.send_buf_.empty()) {
            std::vector<std::uint8_t> &buffer = client.send_buf_.front();
            std::size_t send_size = std::min(
                buffer.size() - client.send_buf_offset_, MAX_DATA - total);
            ssize_t sent =
                send(client.fd(), buffer.data() + client.send_buf_offset_,
                     send_size, 0);

            if (sent == -1) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    // BLOCKING REMOVE
                    remove_flag = true;
                    break;
                } else {
                    throw EpollException("WRITE UNKNOWN ERROR");
                }
            }

            total += sent;

            if (sent == buffer.size() - client.send_buf_offset_) {
                // full amount sent, move to next buffer
                client.send_buf_.pop_front();
                client.send_buf_offset_ = 0;
            } else {
                client.send_buf_offset_ += sent;
            }

            if (sent < send_size) {
                // blocking, remove
                remove_flag = true;
                break;
            }
        }

        if (remove_flag || client.send_buf_.empty()) {
            it = write_list_remove(it);
        } else {
            ++it;
        }
    }
}

void EventLoop::read_list_add(Client &client) {
    read_queue_.push_back(&client);
    client.read_node_ = std::prev(read_queue_.end());
}

std::list<Client *>::iterator
EventLoop::read_list_remove(std::list<Client *>::iterator it) {
    Client *client = *it;
    client->read_node_ = EMPTY_CLIENT_ITER;
    return read_queue_.erase(it);
}

bool EventLoop::in_read_list(Client &client) {
    return client.read_node_ != EMPTY_CLIENT_ITER;
}

void EventLoop::write_list_add(Client &client) {
    write_queue_.push_back(&client);
    client.write_node_ = std::prev(write_queue_.end());
}

std::list<Client *>::iterator
EventLoop::write_list_remove(std::list<Client *>::iterator it) {
    Client *client = *it;
    client->write_node_ = EMPTY_CLIENT_ITER;
    return write_queue_.erase(it);
}

bool EventLoop::in_write_list(Client &client) {
    return client.write_node_ != EMPTY_CLIENT_ITER;
}

} // namespace wolf
