#include <arpa/inet.h>
#include <linux/io_uring.h>
#include <sys/eventfd.h>
#include <sys/socket.h>

#include <wolf.h>

namespace /* internals */ {

/* handle format: Op(4)|THREAD_ID(6)|INDEX(22)|GENERATION(32) */
enum class Op : std::uint8_t { TcpAccept, TcpRead, Wake };

constexpr int OP_BITS = 4;
constexpr int THREAD_ID_BITS = 6;
constexpr int INDEX_BITS = 22;
constexpr int GENERATION_BITS = 32;

constexpr int OP_SHIFT = GENERATION_BITS + INDEX_BITS + THREAD_ID_BITS;
constexpr int THREAD_ID_SHIFT = GENERATION_BITS + INDEX_BITS;
constexpr int INDEX_SHIFT = GENERATION_BITS;
constexpr int GENERATION_SHIFT = 0;

constexpr std::uint64_t THREAD_ID_MASK = ((1LLU << THREAD_ID_BITS) - 1) << THREAD_ID_SHIFT;
constexpr std::uint64_t INDEX_MASK = ((1LLU << INDEX_BITS) - 1) << INDEX_SHIFT;

static_assert(OP_BITS + THREAD_ID_BITS + INDEX_BITS + GENERATION_BITS == 64,
              "handle parts must sum to 64 bits");

constexpr inline std::uint64_t make_handle(int thread_id, int index, std::uint32_t generation) {
    return (std::uint64_t(thread_id) << THREAD_ID_SHIFT) | (std::uint64_t(index) << INDEX_SHIFT) |
           std::uint64_t(generation);
}

constexpr inline std::uint64_t add_operation(std::uint64_t handle, Op op) {
    return handle | (std::uint64_t(op) << OP_SHIFT);
}

constexpr inline Op get_operation(std::uint64_t handle) { return Op(handle >> OP_SHIFT); }

constexpr inline int get_thread_id(std::uint64_t handle) {
    return (handle & THREAD_ID_MASK) >> THREAD_ID_SHIFT;
}

constexpr inline int get_index(std::uint64_t handle) {
    return (handle & INDEX_MASK) >> INDEX_SHIFT;
}

constexpr inline std::uint32_t get_generation(std::uint64_t handle) {
    return handle;
}

thread_local wolf::EventLoop *thread_loop = nullptr;

constexpr std::uint32_t LISTEN_BACKLOG = 4096;
constexpr std::uint32_t RING_ENTRIES_HINT = LISTEN_BACKLOG * 2;

wolf::NetworkError create_listening_socket(std::uint32_t host, std::uint16_t port, int &socket_fd) {
    using wolf::NetworkError;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        switch (errno) {
        case EACCES:
            return NetworkError::PermissionDenied;
        case EMFILE:
            return NetworkError::LimitReached;
        case ENOMEM:
            return NetworkError::NoMemory;
        default:
            return NetworkError::Unknown;
        }
    }

    int val = 1;
    int err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(val));
    if (err != 0) {
        close(fd);
        return NetworkError::Unknown;
    }

    err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (void *)&val, sizeof(val));
    if (err != 0) {
        close(fd);
        return NetworkError::Unknown;
    }

    sockaddr_in addr{
        .sin_family = AF_INET, .sin_port = htons(port), .sin_addr = {.s_addr = htonl(host)}};
    err = bind(fd, (sockaddr *)&addr, sizeof(addr));
    if (err != 0) {
        close(fd);
        switch (errno) {
        case EACCES:
            return NetworkError::PermissionDenied;
        case EADDRINUSE:
            return NetworkError::AddressInUse;
        default:
            return NetworkError::Unknown;
        }
    }

    err = listen(fd, LISTEN_BACKLOG);
    if (err != 0) {
        close(fd);
        switch (errno) {
        case EADDRINUSE:
            return NetworkError::AddressInUse;
        default:
            return NetworkError::Unknown;
        }
    }

    socket_fd = fd;
    return NetworkError::Ok;
}

} // namespace

namespace wolf {

EventLoop::EventLoop(int thread_id)
    : ring_(RING_ENTRIES_HINT), wake_fd_(eventfd(1, 0)), thread_id_(thread_id), tcp_clients_(10),
      tcp_listeners_(1) {
    // add indexes to free list
    for (int i = 0; i < tcp_clients_.size(); i++) {
        tcp_free_clients_.push(i);
    }

    for (int i = 0; i < tcp_listeners_.size(); i++) {
        tcp_free_listeners_.push(i);
    }
}

void EventLoop::post(Message msg) {
    msg_queue_.push(msg);
    wake();
}

void EventLoop::tcp_listen(std::uint32_t host, std::uint16_t port, OnListen on_listen,
                           OnAccept on_accept, OnRead on_read, OnWrite on_write, OnClose on_close) {
    if (thread_loop == this) {
        do_tcp_listen(host, port, on_listen, on_accept, on_read, on_write, on_close);
    } else {
        post({.msg = {.create_listener = {.host = host,
                                          .port = port,
                                          .on_listen = on_listen,
                                          .on_accept = on_accept,
                                          .on_read = on_read,
                                          .on_write = on_write,
                                          .on_close = on_close}},
              .type = MessageType::CreateListener});
    }
}

void EventLoop::wake() { eventfd_write(wake_fd_, 1); }

void EventLoop::handle_cqe(io_uring_cqe *cqe) {
    std::uint64_t handle = cqe->user_data;

    switch (get_operation(handle)) {
    case Op::TcpAccept:
        handle_accept(cqe->user_data, cqe->res, cqe->flags);
        break;
    case Op::TcpRead:
        handle_read(cqe->user_data, cqe->res, cqe->flags);
        break;
    case Op::Wake:
        handle_messages();
        break;
    }
}

void EventLoop::handle_messages() {
    std::vector<Message> messages = msg_queue_.drain();
    for (Message &m : messages) {
        switch (m.type) {
        case MessageType::CreateListener:
            CreateListenerMessage &msg = m.msg.create_listener;
            do_tcp_listen(msg.host, msg.port, msg.on_listen, msg.on_accept, msg.on_read,
                          msg.on_write, msg.on_close);
            break;
        }
    }
    ring_.sq_push({.opcode = IORING_OP_READ,
                   .fd = wake_fd_,
                   .addr = std::bit_cast<std::uint64_t>(&wake_buf_),
                   .len = sizeof(wake_buf_),
                   .user_data = add_operation(0, Op::Wake)});
}

TcpClientView EventLoop::create_client(int fd, OnRead on_read, OnWrite on_write, OnClose on_close) {
    if (tcp_free_clients_.empty()) {
        int size = tcp_clients_.size();
        tcp_clients_.resize(tcp_clients_.size() * 2);
        for (int i = (size * 2) - 1; i >= size; i--) {
            tcp_free_clients_.push(i);
        }
    }

    int index = tcp_free_clients_.top();
    tcp_free_clients_.pop();

    tcp_clients_[index].fd = fd;
    tcp_clients_[index].generation++;
    tcp_clients_[index].on_read = on_read;
    tcp_clients_[index].on_write = on_write;
    tcp_clients_[index].on_close = on_close;

    std::uint64_t handle = make_handle(thread_id_, index, tcp_clients_[index].generation);
    TcpClientView client_view(handle, *this);

    ring_.sq_push({.opcode = IORING_OP_READ,
                   .fd = fd,
                   .addr = std::bit_cast<std::uint64_t>(&tcp_clients_[index].read_buf),
                   .len = sizeof(tcp_clients_[index].read_buf),
                   .user_data = add_operation(handle, Op::TcpRead)});

    return client_view;
}

void EventLoop::handle_accept(std::uint64_t handle, int result, std::uint32_t flags) {
    TcpListener &listener = tcp_listeners_[get_index(handle)];
    if (result >= 0) {
        TcpClientView client_view =
            create_client(result, listener.on_read, listener.on_write, listener.on_close);
        listener.on_accept(*this, client_view, NetworkError::Ok);
    } else {
        // TODO: more specific errors
        TcpClientView client_view(0, *this);
        listener.on_accept(*this, client_view, NetworkError::Unknown);
    }
}

void EventLoop::handle_read(std::uint64_t handle, int result, std::uint32_t flags) {
    TcpClient &client = tcp_clients_[get_index(handle)];

    if (client.generation != get_generation(handle)) {
        // stale request
        return;
    }

    // TODO: deal with result <= 0
    if (result == 0) {
        client.on_read(*this, TcpClientView(handle, *this), nullptr, 0, NetworkError::Unknown);
        return;
    }

    if (result < 0) {
        client.on_read(*this, TcpClientView(handle, *this), nullptr, 0, NetworkError::Unknown);
        return;
    }

    client.on_read(*this, TcpClientView(handle, *this), client.read_buf, result, NetworkError::Ok);
    ring_.sq_push({.opcode = IORING_OP_READ,
                    .fd = client.fd,
                    .addr = std::bit_cast<std::uint64_t>(&client.read_buf),
                    .len = sizeof(client.read_buf),
                    .user_data = add_operation(handle, Op::TcpRead)});
}

void EventLoop::do_tcp_listen(std::uint32_t host, std::uint16_t port, OnListen on_listen,
                              OnAccept on_accept, OnRead on_read, OnWrite on_write,
                              OnClose on_close) {
    int fd;
    NetworkError err = create_listening_socket(host, port, fd);
    TcpListenerView listener(0, *this);

    if (err != NetworkError::Ok) {
        on_listen(*this, listener, err);
        return;
    }

    if (tcp_free_listeners_.empty()) {
        int size = tcp_listeners_.size();
        tcp_listeners_.resize(tcp_listeners_.size() * 2);
        for (int i = (size * 2) - 1; i >= size; i--) {
            tcp_free_listeners_.push(i);
        }
    }

    int index = tcp_free_listeners_.top();
    tcp_free_listeners_.pop();

    tcp_listeners_[index].generation++;
    tcp_listeners_[index].fd = fd;
    tcp_listeners_[index].on_accept = on_accept;
    tcp_listeners_[index].on_read = on_read;
    tcp_listeners_[index].on_write = on_write;
    tcp_listeners_[index].on_close = on_close;

    std::uint64_t handle = make_handle(thread_id_, index, tcp_listeners_[index].generation);
    listener = TcpListenerView(handle, *this);

    // post accept sqe
    ring_.sq_push(io_uring_sqe{.opcode = IORING_OP_ACCEPT,
                               .ioprio = IORING_ACCEPT_MULTISHOT,
                               .fd = fd,
                               .user_data = add_operation(handle, Op::TcpAccept)});

    on_listen(*this, listener, err);
}

void EventLoop::run() {
    thread_loop = this;
    is_running_ = true;

    // arm eventfd wake
    ring_.sq_start_push();
    // Kernel >= 6.7
    ring_.sq_push({.opcode = IORING_OP_READ,
                   .fd = wake_fd_,
                   .addr = std::bit_cast<std::uint64_t>(&wake_buf_),
                   .len = sizeof(wake_buf_),
                   .user_data = add_operation(0, Op::Wake)});
    ring_.sq_end_push();

    while (is_running_) {
        ring_.enter();

        ring_.cq_start_pop();
        ring_.sq_start_push();
        io_uring_cqe *cqe;
        while ((cqe = ring_.cq_pop()) != nullptr) {
            handle_cqe(cqe);
        }
        ring_.sq_end_push();
        ring_.cq_end_pop();
    }
}

} // namespace wolf
