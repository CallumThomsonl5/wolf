#ifndef WOLF_H_INCLUDED
#define WOLF_H_INCLUDED

#include <cstdint>
#include <stack>
#include <vector>

#include <iouring.h>
#include <mpsc_queue.h>

namespace wolf {

// Forward declarations
class EventLoop;
class TcpClientView;
class TcpListenerView;
enum class NetworkError;

// TODO: Come up with the real function signatures
using OnListen = void (*)(EventLoop &, TcpListenerView, NetworkError err);
using OnAccept = void (*)(EventLoop &, TcpClientView, NetworkError err);
using OnConnect = void (*)(void);
using OnRead = void (*)(void);
using OnWrite = void (*)(void);
using OnClose = void (*)(void);

using Handle = std::uint64_t;

/**
 * @brief Errors that can arise during networking.
 */
enum class NetworkError {
    Ok,
    PermissionDenied,
    LimitReached,
    NoMemory,
    AddressInUse,
    Unknown
};

/**
 * @brief Owned and used internally to represent a tcp client connection.
 */
struct TcpClient {
    int fd;
    std::uint32_t generation;
};

/**
 * @brief Owned and used internally to represent a tcp listener.
 */
struct TcpListener {
    int fd;
    OnAccept on_accept;
    OnRead on_read;
    OnWrite on_write;
    OnClose on_close;
    std::uint32_t generation;
};

/**
 * @brief Public interface for tcp client, containing a handle to the underlying
 * representation.
 */
struct TcpClientView {
public:
    TcpClientView(Handle handle, EventLoop &loop)
        : handle_(handle), loop_(&loop) {}

private:
    Handle handle_;
    EventLoop *loop_;
};

/**
 * @brief Public interface for tcp listener, containing a handle to the
 * underlying representation.
 */
struct TcpListenerView {
public:
    TcpListenerView(Handle handle, EventLoop &loop)
        : handle_(handle), loop_(&loop) {}

private:
    Handle handle_;
    EventLoop *loop_;
};

enum class MessageType : std::uint8_t {
    CreateListener
};

struct CreateListenerMessage {
    std::uint32_t host;
    std::uint16_t port;
    OnListen on_listen;
    OnAccept on_accept;
    OnRead on_read;
    OnWrite on_write;
    OnClose on_close;
};

/**
 * @brief Used internally for passing messages between event loops.
 */
struct Message {
    union {
        CreateListenerMessage create_listener;
    } msg;
    MessageType type;
};

/**
 * @brief The main class, handling the event loop.
 */
class EventLoop {
public:
    EventLoop(int thread_id = 0);
    ~EventLoop() = default;

    void post(Message msg);

    void tcp_listen(std::uint32_t host, std::uint16_t port, OnListen on_listen,
                    OnAccept on_accept, OnRead on_read, OnWrite on_write,
                    OnClose on_close);
    void tcp_connect(std::uint32_t host, std::uint16_t port,
                     OnConnect on_connect, OnRead on_read, OnWrite on_write,
                     OnClose on_close);
    void tcp_write(Handle handle, std::uint8_t *buf, std::size_t size);
    void tcp_close(Handle handle);

    void run();
    void stop();

    void wake();

private:
    bool is_running_ = false;

    IOUring ring_;

    std::vector<TcpClient> tcp_clients_;
    std::stack<int> tcp_free_clients_;
    std::vector<TcpListener> tcp_listeners_;
    std::stack<int> tcp_free_listeners_;

    MPSCQueue<Message> msg_queue_;
    int wake_fd_;
    std::uint64_t wake_buf_;
    int thread_id_;

    TcpClientView create_client(int fd);
    void handle_cqe(io_uring_cqe *cqe);
    void handle_messages();
    void handle_accept(Handle handle, int result, std::uint32_t flags);

    void do_tcp_listen(std::uint32_t host, std::uint16_t port, OnListen on_listen, OnAccept on_accept,
                               OnRead on_read, OnWrite on_write,
                               OnClose on_close);
};

inline std::uint32_t ipv4_address(std::uint8_t one, std::uint8_t two,
                                  std::uint8_t three, std::uint8_t four) {
    return (one << 24) | (two << 16) | (three << 8) | four;
}

} // namespace wolf

#endif // WOLF_H_INCLUDED
