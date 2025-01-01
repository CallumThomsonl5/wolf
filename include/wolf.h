#ifndef WOLF_H_INCLUDED
#define WOLF_H_INCLUDED

#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <list>
#include <string>

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

class EpollBase {
public:
    enum class EpollType { Client, Listener };
    virtual EpollType get_type() const = 0;
    // virtual ~EpollBase() = 0;
};

class Client : public EpollBase {
    friend EventLoop;

public:
    using on_recv_t =
        std::function<void(Client &client, std::vector<std::uint8_t> buffer)>;

    Client();
    Client(int fd, TcpListener *listener, EventLoop *loop);
    ~Client();

    EpollType get_type() const override { return EpollType::Client; }
    int fd() const { return fd_; }

    void close();

    void recv(on_recv_t on_recv);
    void stop_recv();
    bool is_recv();

    void send(const std::uint8_t *data, std::size_t size);
    void send(std::vector<std::uint8_t> vec);
    void send(std::string msg);
    void clear_send_buf();

private:
    int fd_ = -1;

    std::deque<std::vector<std::uint8_t>> send_buf_;
    std::size_t send_buf_offset_ = 0;

    TcpListener *listener_ = nullptr;
    EventLoop *loop_ = nullptr;

    on_recv_t on_recv_ = nullptr;

    std::list<Client *>::iterator read_node_;
    std::list<Client *>::iterator write_node_;
    std::list<Client>::iterator loop_node_;
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
    Client &create_client(int fd, TcpListener *listener);

private:
    void poll_io(int timeout);
    void run_read_events();
    void run_write_events();

    std::list<Client *>::iterator
    read_list_remove(std::list<Client *>::iterator it);
    void read_list_add(Client &client);
    bool in_read_list(Client &client);

    bool is_running_ = false;
    int epollfd_ = -1;

    std::list<TcpListener *> listeners_;
    std::list<Client> clients_;
    std::list<Client *> read_queue_;
    std::list<Client *> write_queue_;
};

} // namespace wolf

#endif // WOLF_H_INCLUDED
