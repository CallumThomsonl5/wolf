#ifndef WOLF_H_INCLUDED
#define WOLF_H_INCLUDED

#include <cstdint>
#include <memory>
#include <vector>

#include <linux/io_uring.h>
#include <netinet/in.h>

#include <iouring.h>
#include <mpsc_queue.h>
#include <ringbuffer.h>

namespace wolf {

// Forward declarations
class EventLoop;
class TcpClientView;
class TcpListenerView;
enum class NetworkError;

// TODO: Come up with the real function signatures
using OnListen = void (*)(TcpListenerView, NetworkError err);
using OnAccept = void (*)(TcpClientView, NetworkError err);
using OnConnect = void (*)(TcpClientView, void *context, NetworkError err);
using OnRead = void (*)(TcpClientView, std::uint8_t *buf, std::size_t size, void *context,
                        NetworkError err);
using OnWrite = void (*)(TcpClientView, void *context, NetworkError err);
using OnClose = void (*)(void);

using Handle = std::uint64_t;

/**
 * @brief Errors that can arise during networking.
 */
enum class NetworkError { Ok, PermissionDenied, LimitReached, NoMemory, AddressInUse, Unknown };

/**
 * @brief Owned and used internally to represent a tcp client connection.
 */
struct TcpClient {
    struct Write {
        std::uint8_t *buf;
        std::uint32_t size;
    };
    int fd;
    std::uint32_t generation;
    OnRead on_read;
    OnWrite on_write;
    OnClose on_close;
    std::uint8_t *read_buf;
    RingBuffer<Write> write_queue;
    void *context;
};

/**
 * @brief Owned and used internally to represent a tcp listener.
 */
struct TcpListener {
    int fd;
    OnAccept on_accept;
    std::uint32_t generation;
};

/**
 * @brief Public interface for tcp client, containing a handle to the underlying
 * representation.
 */
struct TcpClientView {
public:
    TcpClientView(Handle handle, EventLoop &loop) : handle_(handle), loop_(&loop) {}

    void write(std::uint8_t *buf, std::uint32_t size);

    void set_context(void *context);
    void set_onread(OnRead on_read);
    void set_onwrite(OnWrite on_write);
    void set_onclose(OnClose on_close);

    EventLoop &loop() { return *loop_; }

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
    TcpListenerView(Handle handle, EventLoop &loop) : handle_(handle), loop_(&loop) {}

    EventLoop &loop() { return *loop_; }

private:
    Handle handle_;
    EventLoop *loop_;
};

enum class MessageType : std::uint8_t {
    CreateListener,
    TcpConnect,
    TcpWrite,
    SetContext,
    SetOnRead,
    SetOnWrite,
    SetOnClose
};

struct CreateListenerMessage {
    std::uint32_t host;
    std::uint16_t port;
    OnListen on_listen;
    OnAccept on_accept;
};

struct ConnectMessage {
    std::uint32_t host;
    std::uint16_t port;
    void *context;
    OnConnect on_connect;
};

struct WriteMessage {
    std::uint8_t *buf;
    std::uint32_t size;
    std::uint64_t handle;
};

struct SetContextMessage {
    void *context;
    std::uint64_t handle;
};

struct SetOnRead {
    OnRead on_read;
    std::uint64_t handle;
};

struct SetOnWrite {
    OnWrite on_write;
    std::uint64_t handle;
};

struct SetOnClose {
    OnClose on_close;
    std::uint64_t handle;
};

/**
 * @brief Used internally for passing messages between event loops.
 */
struct Message {
    union {
        CreateListenerMessage create_listener;
        ConnectMessage connect;
        WriteMessage write;
        SetContextMessage set_context;
        SetOnRead set_onread;
        SetOnWrite set_onwrite;
        SetOnClose set_onclose;
    } msg;
    MessageType type;
};

struct PendingConnection {
    int fd;
    void *context;
    OnConnect on_connect;
    struct sockaddr_in sockaddr{};
};

constexpr std::size_t READ_BUF_SIZE = 4096;

/**
 * @internal
 * @brief Allocator used for client read buffers.
 */
class BufferAllocator {
    static constexpr std::size_t INITIAL_SIZE = 16;
    static_assert(READ_BUF_SIZE % 64 == 0);

public:
    BufferAllocator() { do_chunk_alloc(); }

    ~BufferAllocator() {
        for (std::uint8_t *p : allocations_) {
            std::free(p);
        }
    }

    std::uint8_t *alloc() {
        if (free_list_.empty()) {
            do_chunk_alloc();
        }
        std::uint8_t *ptr = free_list_.back();
        free_list_.pop_back();
        return ptr;
    }

    void free(std::uint8_t *ptr) { free_list_.push_back(ptr); }

private:
    void do_chunk_alloc() {
        std::uint8_t *ptr = static_cast<std::uint8_t *>(
            std::aligned_alloc(64, current_alloc_size_ * READ_BUF_SIZE));

        if (!ptr) {
            throw std::bad_alloc();
        }

        for (std::uint8_t *p = ptr; p < ptr + (current_alloc_size_ * READ_BUF_SIZE);
             p += READ_BUF_SIZE) {
            free_list_.push_back(p);
        }
        allocations_.push_back(ptr);
        current_alloc_size_ *= 2;
    }

    std::vector<std::uint8_t *> free_list_;
    std::vector<std::uint8_t *> allocations_;
    std::size_t current_alloc_size_ = INITIAL_SIZE;
};

/**
 * @brief The main class, handling the event loop.
 */
class EventLoop {
public:
    EventLoop(int thread_id = 0);
    ~EventLoop() = default;

    void post(Message msg);

    void tcp_listen(std::uint32_t host, std::uint16_t port, OnListen on_listen, OnAccept on_accept);
    void tcp_connect(std::uint32_t host, std::uint16_t port, void *context, OnConnect on_connect);
    void tcp_write(Handle handle, std::uint8_t *buf, std::uint32_t size);
    void tcp_close(Handle handle);

    void run();
    void stop();

    void wake();

private:
    bool is_running_ = false;

    IOUring ring_;

    std::vector<std::unique_ptr<PendingConnection>> pending_connections_;
    std::vector<int> free_pending_connections_;

    std::vector<TcpClient> tcp_clients_;
    std::vector<int> tcp_free_clients_;
    std::vector<TcpListener> tcp_listeners_;
    std::vector<int> tcp_free_listeners_;

    BufferAllocator buffer_allocator_;

    MPSCQueue<Message> msg_queue_;
    int wake_fd_;
    std::uint64_t wake_buf_;
    int thread_id_;

    TcpClientView create_client(int fd);
    void handle_cqe(io_uring_cqe *cqe);
    void handle_messages();
    void handle_accept(Handle handle, int result, std::uint32_t flags);
    void handle_socket(Handle handle, int result, std::uint32_t flags);
    void handle_connect(Handle handle, int result, std::uint32_t flags);
    void handle_read(Handle handle, int result, std::uint32_t flags);
    void handle_write(Handle handle, int result, std::uint32_t flags);

    void do_tcp_listen(std::uint32_t host, std::uint16_t port, OnListen on_listen,
                       OnAccept on_accept);
    void do_tcp_connect(std::uint32_t host, std::uint16_t port, void *context,
                        OnConnect on_connect);
    void do_tcp_write(Handle handle, std::uint8_t *buf, std::uint32_t size);
    void do_set_context(Handle handle, void *context);
    void do_set_onread(Handle handle, OnRead on_read);
    void do_set_onwrite(Handle handle, OnWrite on_write);
    void do_set_onclose(Handle handle, OnClose on_close);

    friend struct TcpClientView;
};

inline std::uint32_t ipv4_address(std::uint8_t one, std::uint8_t two, std::uint8_t three,
                                  std::uint8_t four) {
    return (one << 24) | (two << 16) | (three << 8) | four;
}

} // namespace wolf

#endif // WOLF_H_INCLUDED
