#ifndef WOLF_H_INCLUDED
#define WOLF_H_INCLUDED

#include <cstdint>
#include <exception>
#include <functional>
#include <string>

#include <list.h>

namespace wolf {

// Forward declarations
class EventLoop;
class TcpListener;

// exceptions
class EpollException : public std::exception {
public:
    EpollException(const std::string &msg) : msg_(msg) {}
    const char *what() const noexcept override { return msg_.c_str(); }

private:
    const std::string msg_;
};

class ListenerException : public std::exception {
public:
    ListenerException(const std::string &msg) : msg_(msg) {}
    const char *what() const noexcept override { return msg_.c_str(); }

private:
    const std::string msg_;
};

class AcceptException : public std::exception {
public:
    AcceptException(const std::string &msg) : msg_(msg) {}
    const char *what() const noexcept override { return msg_.c_str(); }

private:
    const std::string msg_;
};

enum class EpollType { Client, Listener };
class EpollBase {
public:
    virtual EpollType get_type() const = 0;
    // virtual ~EpollBase() = 0;
};

class Client : public EpollBase {
public:
    using on_recv_t = std::function<void(Client &client)>;

    Client();
    Client(int fd, TcpListener *listener, EventLoop *loop);
    ~Client();

    EpollType get_type() const override { return EpollType::Client; }
    int fd() const { return fd_; }

    void close();

    void recv(on_recv_t on_recv);
    void stop_recv();
    void clear_recv_buf();
    const std::uint8_t *recv_buf() const { return recv_buf_; }

    void send(std::string msg);
    void clear_send_buf();

    ListNode<Client> *node = nullptr;

private:
    int fd_ = -1;

    std::uint8_t *recv_buf_ = nullptr;
    std::size_t recv_buf_size_ = 0;
    std::size_t recv_buf_capacity_ = 0;

    TcpListener *listener_ = nullptr;
    EventLoop *loop_ = nullptr;
};

class TcpListener : public EpollBase {
public:
    using on_connect_t = std::function<void(TcpListener &listener)>;

    TcpListener(std::string host, std::uint16_t port, on_connect_t on_conn);
    ~TcpListener();

    EpollType get_type() const override { return EpollType::Listener; }
    int fd() const { return fd_; }
    void on_conn() { on_conn_(*this); }

    Client *accept(void);

    EventLoop *loop; /**< @brief Points to the event loop */

private:
    std::string host_;
    uint16_t port_;
    int fd_;
    on_connect_t on_conn_;
};

/**
 * @brief The main class, handling the event loop.
 */
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    void run();

    void attach(TcpListener &listener);
    Client *create_client(int fd, TcpListener *listener) {
        Client client(fd, listener, this);
        ListNode<Client> *node = clients_.push_back(std::move(client));
        node->val.node = node;
        return &node->val;
    }

    // void watch(int client, TcpListener &listener, void *data);
    // void unwatch(int client);

private:
    void poll_io(int timeout);

    bool is_running_ = false;
    int epollfd_ = -1;

    List<TcpListener &> listeners_;
    List<Client> clients_;
};

} // namespace wolf

#endif // WOLF_H_INCLUDED
