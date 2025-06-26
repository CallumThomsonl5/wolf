#include <arpa/inet.h>
#include <linux/io_uring.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/socket.h>

#include <wolf.h>

namespace /* internals */ {

/* handle format: Op(4)|THREAD_ID(6)|INDEX(22)|GENERATION(32) */
enum class Op : std::uint8_t { TcpAccept, TcpSocket, TcpConnect, TcpRead, TcpWrite, Wake };

constexpr int OP_BITS = 4;
constexpr int THREAD_ID_BITS = 6;
constexpr int INDEX_BITS = 22;
constexpr int GENERATION_BITS = 32;

constexpr int OP_SHIFT = GENERATION_BITS + INDEX_BITS + THREAD_ID_BITS;
constexpr int THREAD_ID_SHIFT = GENERATION_BITS + INDEX_BITS;
constexpr int INDEX_SHIFT = GENERATION_BITS;
constexpr int GENERATION_SHIFT = 0;

constexpr std::uint64_t OP_MASK = ((1LLU << OP_BITS) - 1) << OP_SHIFT;
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

constexpr inline std::uint64_t remove_operation(std::uint64_t handle) {
    return handle & (~OP_MASK);
}

constexpr inline int get_thread_id(std::uint64_t handle) {
    return (handle & THREAD_ID_MASK) >> THREAD_ID_SHIFT;
}

constexpr inline int get_index(std::uint64_t handle) {
    return (handle & INDEX_MASK) >> INDEX_SHIFT;
}

constexpr inline std::uint32_t get_generation(std::uint64_t handle) { return handle; }

thread_local wolf::EventLoop *thread_loop = nullptr;

constexpr std::uint32_t LISTEN_BACKLOG = 4096;
constexpr std::uint32_t RING_ENTRIES_HINT = 8192;
constexpr std::uint32_t MAX_WRITE_SIZE = 8192;
constexpr std::uint32_t MAX_ACCEPTS = 256;

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

void TcpClientView::write(std::uint8_t *buf, std::uint32_t size) {
    if (thread_loop == loop_) {
        loop_->do_tcp_write(handle_, buf, size);
    } else {
        loop_->post({.msg = {.write{.buf = buf, .size = size, .handle = handle_}},
                     .type = MessageType::TcpWrite});
    }
}

void TcpClientView::set_context(void *context) {
    if (thread_loop == loop_) {
        loop_->do_set_context(handle_, context);
    } else {
        loop_->post({.msg = {.set_context{.context = context, .handle = handle_}},
                     .type = MessageType::SetContext});
    }
}

void TcpClientView::set_onread(OnRead on_read) {
    if (thread_loop == loop_) {
        loop_->do_set_onread(handle_, on_read);
    } else {
        loop_->post({.msg = {.set_onread{.on_read = on_read, .handle = handle_}},
                     .type = MessageType::SetOnRead});
    }
}

void TcpClientView::set_onwrite(OnWrite on_write) {
    if (thread_loop == loop_) {
        loop_->do_set_onwrite(handle_, on_write);
    } else {
        loop_->post({.msg = {.set_onwrite{.on_write = on_write, .handle = handle_}},
                     .type = MessageType::SetOnWrite});
    }
}

void TcpClientView::set_onclose(OnClose on_close) {
    if (thread_loop == loop_) {
        loop_->do_set_onclose(handle_, on_close);
    } else {
        loop_->post({.msg = {.set_onclose{.on_close = on_close, .handle = handle_}},
                     .type = MessageType::SetOnClose});
    }
}

EventLoop::EventLoop(int thread_id)
    : ring_(RING_ENTRIES_HINT), wake_fd_(eventfd(1, 0)), thread_id_(thread_id), tcp_clients_(10),
      tcp_listeners_(1), pending_connections_(10) {
    // add indexes to free list
    for (int i = 0; i < tcp_clients_.size(); i++) {
        tcp_free_clients_.push_back(i);
    }

    for (int i = 0; i < tcp_listeners_.size(); i++) {
        tcp_free_listeners_.push_back(i);
    }

    for (int i = 0; i < pending_connections_.size(); i++) {
        free_pending_connections_.push_back(i);
    }
}

void EventLoop::post(Message msg) {
    msg_queue_.push(msg);
    wake();
}

void EventLoop::tcp_listen(std::uint32_t host, std::uint16_t port, OnListen on_listen,
                           OnAccept on_accept) {
    if (thread_loop == this) {
        do_tcp_listen(host, port, on_listen, on_accept);
    } else {
        post({.msg = {.create_listener =
                          {
                              .host = host,
                              .port = port,
                              .on_listen = on_listen,
                              .on_accept = on_accept,
                          }},
              .type = MessageType::CreateListener});
    }
}

void EventLoop::tcp_connect(std::uint32_t host, std::uint16_t port, void *context,
                            OnConnect on_connect) {
    if (thread_loop == this) {
        do_tcp_connect(host, port, context, on_connect);
    } else {
        post({.msg = {.connect = {.host = host,
                                  .port = port,
                                  .context = context,
                                  .on_connect = on_connect}},
              .type = MessageType::TcpConnect});
    }
}

void EventLoop::wake() { eventfd_write(wake_fd_, 1); }

void EventLoop::handle_cqe(io_uring_cqe *cqe) {
    std::uint64_t handle = cqe->user_data;

    switch (get_operation(handle)) {
    case Op::TcpAccept:
        handle_accept(cqe->user_data, cqe->res, cqe->flags);
        break;
    case Op::TcpSocket:
        handle_socket(cqe->user_data, cqe->res, cqe->flags);
        break;
    case Op::TcpConnect:
        handle_connect(cqe->user_data, cqe->res, cqe->flags);
        break;
    case Op::TcpRead:
        handle_read(cqe->user_data, cqe->res, cqe->flags);
        break;
    case Op::TcpWrite:
        handle_write(cqe->user_data, cqe->res, cqe->flags);
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
        case MessageType::CreateListener: {
            CreateListenerMessage &msg = m.msg.create_listener;
            do_tcp_listen(msg.host, msg.port, msg.on_listen, msg.on_accept);
        } break;
        case MessageType::TcpConnect: {
            ConnectMessage &msg = m.msg.connect;
            do_tcp_connect(msg.host, msg.port, msg.context, msg.on_connect);
        } break;
        case MessageType::TcpWrite: {
            WriteMessage &msg = m.msg.write;
            do_tcp_write(msg.handle, msg.buf, msg.size);
        } break;
        case MessageType::SetContext: {
            SetContextMessage &msg = m.msg.set_context;
            do_set_context(msg.handle, msg.context);
        } break;
        case MessageType::SetOnRead: {
            SetOnRead &msg = m.msg.set_onread;
            do_set_onread(msg.handle, msg.on_read);
        } break;
        case MessageType::SetOnWrite: {
            SetOnWrite &msg = m.msg.set_onwrite;
            do_set_onwrite(msg.handle, msg.on_write);
        } break;
        case MessageType::SetOnClose: {
            SetOnClose &msg = m.msg.set_onclose;
            do_set_onclose(msg.handle, msg.on_close);
        } break;
        }
    }

    ring_.sq_push_read(wake_fd_, reinterpret_cast<std::uint8_t *>(&wake_buf_), sizeof(wake_buf_),
                       add_operation(0, Op::Wake));
}

TcpClientView EventLoop::create_client(int fd) {
    if (tcp_free_clients_.empty()) {
        int size = tcp_clients_.size();
        tcp_clients_.resize(tcp_clients_.size() * 2);
        for (int i = (size * 2) - 1; i >= size; i--) {
            tcp_free_clients_.push_back(i);
        }
    }

    int index = tcp_free_clients_.back();
    tcp_free_clients_.pop_back();

    tcp_clients_[index].fd = fd;
    tcp_clients_[index].generation++;
    tcp_clients_[index].read_buf = buffer_allocator_.alloc();
    tcp_clients_[index].write_queue.clear();

    std::uint64_t handle = make_handle(thread_id_, index, tcp_clients_[index].generation);
    TcpClientView client_view(handle, *this);

    ring_.sq_push_read(fd, tcp_clients_[index].read_buf, READ_BUF_SIZE,
                       add_operation(handle, Op::TcpRead));

    return client_view;
}

void EventLoop::handle_accept(std::uint64_t handle, int result, std::uint32_t flags) {
    TcpListener &listener = tcp_listeners_[get_index(handle)];
    if (result >= 0) {
        TcpClientView client_view = create_client(result);
        listener.on_accept(client_view, NetworkError::Ok);
    } else {
        // TODO: more specific errors
        TcpClientView client_view(0, *this);
        listener.on_accept(client_view, NetworkError::Unknown);
    }
    ring_.sq_push_accept(listener.fd, add_operation(handle, Op::TcpAccept));
}

void EventLoop::handle_socket(Handle handle, int result, std::uint32_t flags) {
    PendingConnection &pending = pending_connections_[get_index(handle)];

    if (result < 0) {
        // TODO: more detailed error
        pending.on_connect(TcpClientView(0, *this), pending.context, NetworkError::Unknown);
        free_pending_connections_.push_back(get_index(handle));
        return;
    }

    pending.fd = result;
    ring_.sq_push_connect(result, reinterpret_cast<sockaddr *>(&pending.sockaddr),
                          sizeof(pending.sockaddr),
                          add_operation(remove_operation(handle), Op::TcpConnect));
}

void EventLoop::handle_connect(Handle handle, int result, std::uint32_t flags) {
    PendingConnection &pending = pending_connections_[get_index(handle)];

    if (result < 0) {
        // TODO: more detailed error
        pending.on_connect(TcpClientView(0, *this), pending.context, NetworkError::Unknown);
    } else {
        TcpClientView clientview = create_client(pending.fd);
        clientview.set_context(pending.context);
        pending.on_connect(clientview, pending.context, NetworkError::Ok);
    }

    free_pending_connections_.push_back(get_index(handle));
}

void EventLoop::handle_read(std::uint64_t handle, int result, std::uint32_t flags) {
    TcpClient &client = tcp_clients_[get_index(handle)];

    if (client.generation != get_generation(handle)) {
        // stale request
        return;
    }

    // TODO: deal with result <= 0
    if (result == 0) {
        client.on_read(TcpClientView(remove_operation(handle), *this), nullptr, 0, client.context,
                       NetworkError::Unknown);
        return;
    }

    if (result < 0) {
        client.on_read(TcpClientView(remove_operation(handle), *this), nullptr, 0, client.context,
                       NetworkError::Unknown);
        return;
    }

    client.on_read(TcpClientView(handle, *this), client.read_buf, result, client.context,
                   NetworkError::Ok);

    ring_.sq_push_read(client.fd, client.read_buf, READ_BUF_SIZE,
                       add_operation(handle, Op::TcpRead));
}

void EventLoop::handle_write(std::uint64_t handle, int result, std::uint32_t flags) {
    TcpClient &client = tcp_clients_[get_index(handle)];
    if (client.generation != get_generation(handle)) {
        // stale request
        return;
    }

    if (result < 0) {
        // TODO: deal with this
        return;
    }

    TcpClient::Write &write = client.write_queue.front();
    if (result < write.size) {
        write.buf += result;
        write.size -= result;
    } else {
        client.on_write(TcpClientView(remove_operation(handle), *this), client.context,
                        NetworkError::Ok);
        client.write_queue.pop_front();
    }

    if (!client.write_queue.empty()) {
        ring_.sq_push_write(client.fd, client.write_queue.front().buf,
                            client.write_queue.front().size, add_operation(handle, Op::TcpWrite));
    }
}

void EventLoop::do_tcp_listen(std::uint32_t host, std::uint16_t port, OnListen on_listen,
                              OnAccept on_accept) {
    int fd;
    NetworkError err = create_listening_socket(host, port, fd);
    TcpListenerView listener(0, *this);

    if (err != NetworkError::Ok) {
        on_listen(listener, err);
        return;
    }

    if (tcp_free_listeners_.empty()) {
        int size = tcp_listeners_.size();
        tcp_listeners_.resize(tcp_listeners_.size() * 2);
        for (int i = (size * 2) - 1; i >= size; i--) {
            tcp_free_listeners_.push_back(i);
        }
    }

    int index = tcp_free_listeners_.back();
    tcp_free_listeners_.pop_back();

    tcp_listeners_[index].generation++;
    tcp_listeners_[index].fd = fd;
    tcp_listeners_[index].on_accept = on_accept;

    std::uint64_t handle = make_handle(thread_id_, index, tcp_listeners_[index].generation);
    listener = TcpListenerView(handle, *this);

    // post accept sqe
    for (int i = 0; i < MAX_ACCEPTS; i++) {
        ring_.sq_push_accept(fd, add_operation(handle, Op::TcpAccept));
    }
    on_listen(listener, err);
}

void EventLoop::do_tcp_connect(std::uint32_t host, std::uint16_t port, void *context,
                               OnConnect on_connect) {
    if (free_pending_connections_.empty()) {
        int size = pending_connections_.size();
        pending_connections_.resize(pending_connections_.size() * 2);
        for (int i = (size * 2) - 1; i >= size; i--) {
            free_pending_connections_.push_back(i);
        }
    }

    int index = free_pending_connections_.back();
    free_pending_connections_.pop_back();

    pending_connections_[index].context = context;
    pending_connections_[index].on_connect = on_connect;
    pending_connections_[index].sockaddr = sockaddr_in{
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = {.s_addr = htonl(host)},
    };

    ring_.sq_push_socket(AF_INET, SOCK_STREAM, 0,
                         add_operation(make_handle(thread_id_, index, 0), Op::TcpSocket));
}

void EventLoop::do_tcp_write(std::uint64_t handle, std::uint8_t *buf, std::uint32_t size) {
    TcpClient &client = tcp_clients_[get_index(handle)];

    if (client.generation != get_generation(handle)) {
        // stale
        return;
    }

    client.write_queue.push_back({.buf = buf, .size = size});

    if (!client.write_queue.empty()) {
        ring_.sq_push_write(client.fd, buf, size, add_operation(handle, Op::TcpWrite));
    }
}

void EventLoop::do_set_context(std::uint64_t handle, void *context) {
    TcpClient &client = tcp_clients_[get_index(handle)];
    if (client.generation != get_generation(handle)) {
        return;
    }
    client.context = context;
}

void EventLoop::do_set_onread(std::uint64_t handle, OnRead on_read) {
    TcpClient &client = tcp_clients_[get_index(handle)];
    if (client.generation != get_generation(handle)) {
        return;
    }
    client.on_read = on_read;
}

void EventLoop::do_set_onwrite(std::uint64_t handle, OnWrite on_write) {
    TcpClient &client = tcp_clients_[get_index(handle)];
    if (client.generation != get_generation(handle)) {
        return;
    }
    client.on_write = on_write;
}

void EventLoop::do_set_onclose(std::uint64_t handle, OnClose on_close) {
    TcpClient &client = tcp_clients_[get_index(handle)];
    if (client.generation != get_generation(handle)) {
        return;
    }
    client.on_close = on_close;
}

void EventLoop::run() {
    thread_loop = this;
    is_running_ = true;

    // arm eventfd wake
    ring_.sq_start_push();
    ring_.sq_push_read(wake_fd_, reinterpret_cast<std::uint8_t *>(wake_buf_), sizeof(wake_buf_),
                       add_operation(0, Op::Wake));
    ring_.sq_end_push();

    while (is_running_) {
        ring_.enter();

        ring_.cq_start_pop();
        ring_.sq_start_push();

        io_uring_cqe *cqe;
        while ((cqe = ring_.cq_pop()) != nullptr) {
            handle_cqe(cqe);
        }

        while (!ring_.sq_full() && !overflow_sqes_.empty()) {
            ring_.sq_push(overflow_sqes_.front());
            overflow_sqes_.pop_front();
        }

        ring_.sq_end_push();
        ring_.cq_end_pop();
    }

    thread_loop = nullptr;
}

} // namespace wolf
