#ifndef WOLF_H_INCLUDED
#define WOLF_H_INCLUDED

#include "internal/iouring.h"
#include "internal/mpsc_queue.h"
#include "internal/ringbuffer.h"
#include "internal/timers.h"

#include <cstdio>
#include <fcntl.h>
#include <linux/io_uring.h>
#include <netinet/in.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace wolf {

// Forward declarations
class EventLoop;
class TcpClientView;
class TcpListenerView;
class FileView;
enum class NetworkError;
enum class FileError;

using OnListen = void (*)(TcpListenerView, NetworkError err);
using OnAccept = void (*)(TcpClientView, NetworkError err);
using OnConnect = void (*)(TcpClientView, void *context, NetworkError err);
using OnRecv = void (*)(TcpClientView, std::uint8_t *buf, std::size_t size, void *context,
                        NetworkError err);
using OnSend = void (*)(TcpClientView, std::uint8_t *buf, std::size_t size, void *context,
                        void *send_ctx, NetworkError err);
using OnTcpClose = void (*)(TcpClientView client, void *context, NetworkError err);

using OnOpen = void (*)(FileView file, void *context, FileError err);
using OnFileClose = void (*)();
using OnRead = void (*)();
using OnWrite = void (*)();

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
    PeerShutdownWrite,
    Closed,
    Unknown
};

/**
 * @brief Errors relating to files
 */
enum class FileError {
    Ok,
    DoesNotExist,
    InsufficientPermissions,
    AlreadyExists,
    IsDirectory,
    NoSpace,
    Unknown
};

enum class FileOptions {
    None = 0,
    Create = O_CREAT,
    Direct = O_DIRECT,
    Tmp = O_TMPFILE,
    Trunc = O_TRUNC,
    Sync = O_SYNC
};

enum class FileWriteFlags {
    None,
    Sync,
    AtomicAppend,
    FastAppend
};

inline constexpr FileOptions operator|(FileOptions a, FileOptions b) {
    return static_cast<FileOptions>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline constexpr void operator|=(FileOptions &a, FileOptions b) { a = a | b; }

enum class FileMode { Read = O_RDONLY, Write = O_WRONLY, RdWr = O_RDWR };

enum class CloseType { Abort, Graceful };

/**
 * @brief Owned and used internally to represent a tcp client connection.
 */
struct TcpClient {
    struct Send {
        void *context;
        std::uint8_t *buf;
        std::uint32_t size;
    };

    int fd;
    std::uint32_t generation;

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
    std::uint8_t *read_buf;
    internal::RingBuffer<Send, 4> send_queue;
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
 * @internal
 * @brief Internal File representation
 */
struct File {
    struct FileOp {
        enum class Type : std::uint8_t {
            Read,
            Write,
        };
        Type type;
        std::size_t pos;
        std::size_t length;
        std::uint8_t *buf;
        void *op_context;
        int flags;
    };

    int fd;
    void *context;
    OnOpen on_open;
    OnRead on_read;
    OnWrite on_write;
    OnFileClose on_close;
    int inflight_ops;
    int flags;
    std::size_t append_pos;
    std::uint32_t generation;
    internal::RingBuffer<FileOp, 64> ops_queue;
};

class FileView {
public:
    FileView(Handle handle, EventLoop &loop) : handle_(handle), loop_(&loop) {}

    void write_to(std::size_t pos, const std::uint8_t *buf, std::size_t len, void *write_ctx,
                  OnWrite on_write);
    void read_from(std::size_t pos, std::uint8_t *buf, std::size_t len, void *read_ctx,
                   OnRead on_read);

    void append_safe(const std::uint8_t *buf, std::size_t len, void *write_ctx, OnWrite on_write);
    void append_fast(const std::uint8_t *buf, std::size_t len, void *write_ctx, OnWrite on_write);

    void close();

private:
    Handle handle_;
    EventLoop *loop_;
};

/**
 * @brief Public interface for tcp client, containing a handle to the underlying
 * representation.
 */
class TcpClientView {
public:
    TcpClientView(Handle handle, EventLoop &loop) : handle_(handle), loop_(&loop) {}

    void send(std::uint8_t *buf, std::uint32_t size, void *send_ctx);
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

enum class MessageType : std::uint8_t {
    CreateListener,
    TcpConnect,
    TcpSend,
    TcpClose,
    SetContext,
    SetOnRecv,
    SetOnSend,
    SetOnClose,
    FileOpen,
    FileWrite,
    FileRead
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

struct SendMessage {
    void *context;
    std::uint8_t *buf;
    std::uint64_t handle;
    std::uint32_t size;
};

struct CloseMessage {
    CloseType type;
    std::uint64_t handle;
};

struct SetContextMessage {
    void *context;
    std::uint64_t handle;
};

struct SetOnRecvMessage {
    OnRecv on_read;
    std::uint64_t handle;
};

struct SetOnSendMessage {
    OnSend on_write;
    std::uint64_t handle;
};

struct SetOnCloseMessage {
    OnTcpClose on_close;
    std::uint64_t handle;
};

struct FileOpenMessage {
    const char *path;
    FileMode mode;
    FileOptions options;
    int perms;
    void *context;
    OnOpen on_open;
};

struct FileWriteMessage {
    FileWriteFlags flags;
    std::size_t pos;
    const std::uint8_t *buf;
    std::size_t len;
    void *write_ctx;
    OnWrite on_write;
    std::uint64_t handle;
};

struct FileReadMessage {
    std::size_t pos;
    std::uint8_t *buf;
    std::size_t len;
    void *read_ctx;
    OnRead on_read;
    std::uint64_t handle;
};

/**
 * @brief Used internally for passing messages between event loops.
 */
struct Message {
    union {
        CreateListenerMessage create_listener;
        ConnectMessage connect;
        SendMessage send;
        CloseMessage close;
        SetContextMessage set_context;
        SetOnRecvMessage set_onrecv;
        SetOnSendMessage set_onsend;
        SetOnCloseMessage set_onclose;
        FileOpenMessage file_open;
        FileWriteMessage file_write;
        FileReadMessage file_read;
    } msg;
    MessageType type;
};

struct PendingConnection {
    int fd;
    void *context;
    OnConnect on_connect;
    struct sockaddr_in sockaddr{};
};

constexpr std::size_t READ_BUF_SIZE = 65536;

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
    explicit EventLoop(int thread_id = 0);
    ~EventLoop() = default;

    void post(Message msg);

    void tcp_listen(std::uint32_t host, std::uint16_t port, OnListen on_listen, OnAccept on_accept);
    void tcp_connect(std::uint32_t host, std::uint16_t port, void *context, OnConnect on_connect);
    void tcp_send(Handle handle, std::uint8_t *buf, std::uint32_t size, void *send_ctx);
    void tcp_close(Handle handle);

    void file_open(const char *path, FileMode mode, FileOptions options, int perms, void *context,
                   OnOpen on_open);

    Handle set_timeout(OnTimeout on_timeout, void *context, std::uint64_t);
    Handle set_interval(OnTimeout on_timeout, void *context, std::uint64_t);
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

    std::vector<File> files_;
    std::vector<int> free_files_;

    internal::MPSCQueue<Message> msg_queue_;
    int wake_fd_;
    std::uint64_t wake_buf_;
    int thread_id_;

    TcpClientView create_client(int fd);
    void handle_cqe(io_uring_cqe *cqe);
    void handle_messages();
    void handle_accept(Handle handle, int result, std::uint32_t flags);
    void handle_socket(Handle handle, int result, std::uint32_t flags);
    void handle_connect(Handle handle, int result, std::uint32_t flags);
    void handle_recv(Handle handle, int result, std::uint32_t flags);
    void handle_send(Handle handle, int result, std::uint32_t flags);
    void handle_shutdown_wr(Handle handle, int result, std::uint32_t flags);
    void handle_shutdown_rdwr(Handle handle, int result, std::uint32_t flags);
    void handle_close(Handle handle, int result, std::uint32_t flags);
    void handle_file_open(Handle handle, int result, std::uint32_t flags);
    void handle_file_read(Handle handle, int result, std::uint32_t flags);

    void do_tcp_listen(std::uint32_t host, std::uint16_t port, OnListen on_listen,
                       OnAccept on_accept);
    void do_tcp_connect(std::uint32_t host, std::uint16_t port, void *context,
                        OnConnect on_connect);
    void do_tcp_send(Handle handle, std::uint8_t *buf, std::uint32_t size, void *send_ctx);
    void do_tcp_close(Handle handle, CloseType type = CloseType::Graceful);
    void do_set_context(Handle handle, void *context);
    void do_set_onrecv(Handle handle, OnRecv on_read);
    void do_set_onsend(Handle handle, OnSend on_write);
    void do_set_onclose(Handle handle, OnTcpClose on_close);

    void do_file_open(const char *path, FileMode mode, FileOptions options, int perms,
                      void *context, OnOpen on_open);
    void do_file_write(Handle handle ,std::size_t pos, const std::uint8_t *buf, std::size_t len, FileWriteFlags flags, void *write_ctx,
                        OnWrite on_write);
    void do_file_read_from(Handle handle, std::size_t pos, std::uint8_t *buf, std::size_t len, void *read_ctx,
                   OnRead on_read);

    friend class TcpClientView;
    friend class FileView;
};

inline std::uint32_t ipv4_address(std::uint8_t one, std::uint8_t two, std::uint8_t three,
                                  std::uint8_t four) {
    return (one << 24) | (two << 16) | (three << 8) | four;
}

} // namespace wolf

#endif // WOLF_H_INCLUDED
