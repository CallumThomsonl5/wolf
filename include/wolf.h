#ifndef WOLF_H_INCLUDED
#define WOLF_H_INCLUDED

#include <internal/iouring.h>
#include <internal/mpsc_queue.h>
#include <internal/ringbuffer.h>
#include <internal/timers.h>
#include <internal/files.h>
#include <internal/messages.h>

#include <types.h>
#include <files.h>

#include <cstdio>
#include <fcntl.h>
#include <linux/io_uring.h>
#include <netinet/in.h>

#include <cstdint>
#include <vector>

namespace wolf {

/**
 * @brief Owned and used internally to represent a tcp client connection.
 */
struct TcpClient {
    struct Send {
        void *context;
        uint8_t *buf;
        uint32_t size;
    };

    int fd;
    uint32_t generation;

    // State
    bool recv_pending;
    bool send_pending;
    bool read_side_open;
    bool write_side_open;
    bool wr_shutdown_sent;
    bool rdwr_shutdown_sent;
    bool closing;
    bool close_sent;

    OnRecv on_recv;
    OnSend on_send;
    OnTcpClose on_close;
    uint8_t *read_buf;
    internal::RingBuffer<Send, 4> send_queue;
    void *context;
};

/**
 * @brief Owned and used internally to represent a tcp listener.
 */
struct TcpListener {
    int fd;
    OnAccept on_accept;
    uint32_t generation;
};

/**
 * @brief Public interface for tcp client, containing a handle to the underlying
 * representation.
 */
class TcpClientView {
public:
    TcpClientView(Handle handle, EventLoop &loop) : handle_(handle), loop_(&loop) {}

    void send(uint8_t *buf, uint32_t size, void *send_ctx);
    void close(CloseType type = CloseType::Graceful);

    void set_context(void *context);
    void set_onrecv(OnRecv on_recv);
    void set_onsend(OnSend on_send);
    void set_onclose(OnTcpClose on_close);

    EventLoop &loop() { return *loop_; }

private:
    Handle handle_;
    EventLoop *loop_;
};

/**
 * @brief Public interface for tcp listener, containing a handle to the
 * underlying representation.
 */
class TcpListenerView {
public:
    TcpListenerView(Handle handle, EventLoop &loop) : handle_(handle), loop_(&loop) {}

    EventLoop &loop() { return *loop_; }

private:
    Handle handle_;
    EventLoop *loop_;
};

struct PendingConnection {
    int fd;
    void *context;
    OnConnect on_connect;
    struct sockaddr_in sockaddr{};
};

constexpr size_t READ_BUF_SIZE = 65536;

/**
 * @internal
 * @brief Allocator used for client read buffers.
 */
class BufferAllocator {
    static constexpr size_t INITIAL_SIZE = 16;
    static_assert(READ_BUF_SIZE % 64 == 0);

public:
    BufferAllocator() { do_chunk_alloc(); }

    ~BufferAllocator() {
        for (uint8_t *p : allocations_) {
            std::free(p);
        }
    }

    uint8_t *alloc() {
        if (free_list_.empty()) {
            do_chunk_alloc();
        }
        uint8_t *ptr = free_list_.back();
        free_list_.pop_back();
        return ptr;
    }

    void free(uint8_t *ptr) { free_list_.push_back(ptr); }

private:
    void do_chunk_alloc() {
        uint8_t *ptr = static_cast<uint8_t *>(
            std::aligned_alloc(64, current_alloc_size_ * READ_BUF_SIZE));

        if (!ptr) {
            throw std::bad_alloc();
        }

        for (uint8_t *p = ptr; p < ptr + (current_alloc_size_ * READ_BUF_SIZE);
             p += READ_BUF_SIZE) {
            free_list_.push_back(p);
        }
        allocations_.push_back(ptr);
        current_alloc_size_ *= 2;
    }

    std::vector<uint8_t *> free_list_;
    std::vector<uint8_t *> allocations_;
    size_t current_alloc_size_ = INITIAL_SIZE;
};

/**
 * @brief The main class, handling the event loop.
 */
class EventLoop {
public:
    explicit EventLoop(int thread_id = 0);
    ~EventLoop() = default;

    void tcp_listen(uint32_t host, uint16_t port, OnListen on_listen, OnAccept on_accept);
    void tcp_connect(uint32_t host, uint16_t port, void *context, OnConnect on_connect);
    void tcp_send(Handle handle, uint8_t *buf, uint32_t size, void *send_ctx);
    void tcp_close(Handle handle);


    void file_open(const char *path, FileOpenMode mode, FileOpenOptions options, int perms, void *context,
                   OnOpen on_open, OnFileClose on_close);
    void file_read_from(Handle handle, size_t off, uint8_t *buf, size_t len, uint64_t token);
    void file_write_to(Handle handle, size_t off, const uint8_t *buf, size_t len, uint64_t token);
    void file_close(Handle handle);

    void file_set_onread(Handle handle, OnRead on_read);
    void file_set_onwrite(Handle handle, OnWrite on_write);


    Handle set_timeout(OnTimeout on_timeout, void *context, uint64_t);
    Handle set_interval(OnTimeout on_timeout, void *context, uint64_t);
    void cancel_timer(Handle handle);

    void run();
    void stop();

    void wake();

private:
    bool is_running_ = false;

    internal::IOUring ring_;

    std::vector<std::unique_ptr<PendingConnection>> pending_connections_;
    std::vector<int> free_pending_connections_;

    std::vector<TcpClient> tcp_clients_;
    std::vector<int> tcp_free_clients_;
    std::vector<TcpListener> tcp_listeners_;
    std::vector<int> tcp_free_listeners_;

    BufferAllocator buffer_allocator_;

    internal::TimeType time_;
    std::vector<internal::Timer> timers_;
    std::vector<int> free_timers_;
    internal::TimerHeap timer_heap_;

    internal::FileSubsystem file_;

    internal::MPSCQueue<internal::Message> msg_queue_;
    int wake_fd_;
    uint64_t wake_buf_;
    int thread_id_;

    void post(internal::Message msg);

    TcpClientView create_client(int fd);
    void handle_cqe(io_uring_cqe *cqe);
    void handle_messages();
    void handle_accept(Handle handle, int result, uint32_t flags);
    void handle_socket(Handle handle, int result, uint32_t flags);
    void handle_connect(Handle handle, int result, uint32_t flags);
    void handle_recv(Handle handle, int result, uint32_t flags);
    void handle_send(Handle handle, int result, uint32_t flags);
    void handle_shutdown_wr(Handle handle, int result, uint32_t flags);
    void handle_shutdown_rdwr(Handle handle, int result, uint32_t flags);
    void handle_close(Handle handle, int result, uint32_t flags);

    void do_tcp_listen(uint32_t host, uint16_t port, OnListen on_listen,
                       OnAccept on_accept);
    void do_tcp_connect(uint32_t host, uint16_t port, void *context,
                        OnConnect on_connect);
    void do_tcp_send(Handle handle, uint8_t *buf, uint32_t size, void *send_ctx);
    void do_tcp_close(Handle handle, CloseType type = CloseType::Graceful);
    void do_set_context(Handle handle, void *context);
    void do_set_onrecv(Handle handle, OnRecv on_read);
    void do_set_onsend(Handle handle, OnSend on_write);
    void do_set_onclose(Handle handle, OnTcpClose on_close);

    friend class TcpClientView;
};

inline uint32_t ipv4_address(uint8_t one, uint8_t two, uint8_t three,
                                  uint8_t four) {
    return (one << 24) | (two << 16) | (three << 8) | four;
}

} // namespace wolf

#endif // WOLF_H_INCLUDED

