#include "wolf.h"

#include "handle.h"

#include <arpa/inet.h>
#include <memory>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/socket.h>


namespace /* internals */ {

constexpr std::uint32_t LISTEN_BACKLOG = 4096;
constexpr std::uint32_t RING_ENTRIES_HINT = 8192;
constexpr std::uint32_t MAX_WRITE_SIZE = 65536;

thread_local wolf::EventLoop *thread_loop = nullptr;

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

void TcpClientView::send(std::uint8_t *buf, std::uint32_t size) {
    if (thread_loop == loop_) {
        loop_->do_tcp_send(handle_, buf, size);
    } else {
        loop_->post({.msg = {.send{.buf = buf, .size = size, .handle = handle_}},
                     .type = MessageType::TcpSend});
    }
}

void TcpClientView::close(CloseType type) {
    if (thread_loop == loop_) {
        loop_->do_tcp_close(handle_, type);
    } else {
        loop_->post(
            {.msg = {.close{.type = type, .handle = handle_}}, .type = MessageType::TcpClose});
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

void TcpClientView::set_onrecv(OnRecv on_recv) {
    if (thread_loop == loop_) {
        loop_->do_set_onrecv(handle_, on_recv);
    } else {
        loop_->post({.msg = {.set_onrecv{.on_read = on_recv, .handle = handle_}},
                     .type = MessageType::SetOnRecv});
    }
}

void TcpClientView::set_onsend(OnSend on_send) {
    if (thread_loop == loop_) {
        loop_->do_set_onsend(handle_, on_send);
    } else {
        loop_->post({.msg = {.set_onsend{.on_write = on_send, .handle = handle_}},
                     .type = MessageType::SetOnSend});
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
        pending_connections_[i] = std::make_unique<PendingConnection>();
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

    switch (handle::get_op(handle)) {
    case handle::Op::TcpAccept:
        handle_accept(cqe->user_data, cqe->res, cqe->flags);
        break;
    case handle::Op::TcpSocket:
        handle_socket(cqe->user_data, cqe->res, cqe->flags);
        break;
    case handle::Op::TcpConnect:
        handle_connect(cqe->user_data, cqe->res, cqe->flags);
        break;
    case handle::Op::TcpRecv:
        handle_recv(cqe->user_data, cqe->res, cqe->flags);
        break;
    case handle::Op::TcpSend:
        handle_send(cqe->user_data, cqe->res, cqe->flags);
        break;
    case handle::Op::TcpShutdownWr:
        handle_shutdown_wr(cqe->user_data, cqe->res, cqe->flags);
        break;
    case handle::Op::TcpShutdownRdWr:
        handle_shutdown_rdwr(cqe->user_data, cqe->res, cqe->flags);
        break;
    case handle::Op::TcpClose:
        handle_close(cqe->user_data, cqe->res, cqe->flags);
        break;
    case handle::Op::Timer:
        // Silence error
        break;
    case handle::Op::Wake:
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
        case MessageType::TcpSend: {
            SendMessage &msg = m.msg.send;
            do_tcp_send(msg.handle, msg.buf, msg.size);
        } break;
        case MessageType::TcpClose: {
            CloseMessage &msg = m.msg.close;
            do_tcp_close(msg.handle, msg.type);
        } break;
        case MessageType::SetContext: {
            SetContextMessage &msg = m.msg.set_context;
            do_set_context(msg.handle, msg.context);
        } break;
        case MessageType::SetOnRecv: {
            SetOnRecvMessage &msg = m.msg.set_onrecv;
            do_set_onrecv(msg.handle, msg.on_read);
        } break;
        case MessageType::SetOnSend: {
            SetOnSendMessage &msg = m.msg.set_onsend;
            do_set_onsend(msg.handle, msg.on_write);
        } break;
        case MessageType::SetOnClose: {
            SetOnCloseMessage &msg = m.msg.set_onclose;
            do_set_onclose(msg.handle, msg.on_close);
        } break;
        }
    }

    ring_.sq_push_read(wake_fd_, reinterpret_cast<std::uint8_t *>(&wake_buf_), sizeof(wake_buf_),
                       handle::set_op(0, handle::Op::Wake));
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
    tcp_clients_[index].read_buf = buffer_allocator_.alloc();
    tcp_clients_[index].send_queue.clear();

    tcp_clients_[index].recv_pending = true;
    tcp_clients_[index].send_pending = false;
    tcp_clients_[index].read_side_open = true;
    tcp_clients_[index].write_side_open = true;
    tcp_clients_[index].wr_shutdown_sent = false;
    tcp_clients_[index].rdwr_shutdown_sent = false;
    tcp_clients_[index].closing = false;
    tcp_clients_[index].close_sent = false;

    std::uint64_t handle = handle::make(thread_id_, index, tcp_clients_[index].generation);
    TcpClientView client_view(handle, *this);

    ring_.sq_push_recv(fd, tcp_clients_[index].read_buf, READ_BUF_SIZE,
                       handle::set_op(handle, handle::Op::TcpRecv));

    return client_view;
}

void EventLoop::handle_accept(std::uint64_t handle, int result, std::uint32_t flags) {
    TcpListener &listener = tcp_listeners_[handle::get_index(handle)];
    if (result >= 0) {
        TcpClientView client_view = create_client(result);
        listener.on_accept(client_view, NetworkError::Ok);
    } else {
        // TODO: more specific errors
        TcpClientView client_view(0, *this);
        listener.on_accept(client_view, NetworkError::Unknown);
    }

    if (!(flags & IORING_CQE_F_MORE)) {
        ring_.sq_push_accept(listener.fd, handle::set_op(handle, handle::Op::TcpAccept));
    }
}

void EventLoop::handle_socket(Handle handle, int result, std::uint32_t flags) {
    PendingConnection &pending = *pending_connections_[handle::get_index(handle)].get();

    if (result < 0) {
        // TODO: more detailed error
        pending.on_connect(TcpClientView(0, *this), pending.context, NetworkError::Unknown);
        free_pending_connections_.push_back(handle::get_index(handle));
        return;
    }

    pending.fd = result;
    ring_.sq_push_connect(result, reinterpret_cast<sockaddr *>(&pending.sockaddr),
                          sizeof(pending.sockaddr), handle::set_op(handle, handle::Op::TcpConnect));
}

void EventLoop::handle_connect(Handle handle, int result, std::uint32_t flags) {
    PendingConnection &pending = *pending_connections_[handle::get_index(handle)].get();

    if (result < 0) {
        // TODO: more detailed error
        pending.on_connect(TcpClientView(0, *this), pending.context, NetworkError::Unknown);
    } else {
        TcpClientView clientview = create_client(pending.fd);
        clientview.set_context(pending.context);
        pending.on_connect(clientview, pending.context, NetworkError::Ok);
    }

    free_pending_connections_.push_back(handle::get_index(handle));
}

void EventLoop::handle_recv(std::uint64_t handle, int result, std::uint32_t flags) {
    TcpClient &client = tcp_clients_[handle::get_index(handle)];

    if (client.generation != handle::get_gen(handle)) {
        // stale request
        return;
    }

    client.recv_pending = false;

    if (client.closing) {
        if (!client.send_pending && !client.close_sent) {
            ring_.sq_push_close(client.fd, handle::set_op(handle, handle::Op::TcpClose));
            client.close_sent = true;
        }
        return;
    }

    if (result < 0) {
        // TODO: pass the real error instead of unknown
        client.on_recv(TcpClientView(handle, *this), nullptr, 0, client.context,
                       NetworkError::Unknown);

        // TODO: set close reason
        do_tcp_close(handle, CloseType::Abort);

        return;
    }

    if (result == 0) {
        client.read_side_open = false;
        if (!client.write_side_open && !client.send_pending) {
            client.close_sent = true;
            ring_.sq_push_close(client.fd, handle::set_op(handle, handle::Op::TcpClose));
        } else {
            client.on_close(TcpClientView(handle, *this), client.context,
                            NetworkError::PeerShutdownWrite);
        }

        return;
    }

    // Sucess path
    client.on_recv(TcpClientView(handle, *this), client.read_buf, result, client.context,
                   NetworkError::Ok);
    if (client.closing) {
        if (!client.send_pending && !client.close_sent) {
            ring_.sq_push_close(client.fd, handle::set_op(handle, handle::Op::TcpClose));
            client.close_sent = true;
        }
        return;
    }
    ring_.sq_push_recv(client.fd, client.read_buf, READ_BUF_SIZE,
                       handle::set_op(handle, handle::Op::TcpRecv));
    client.recv_pending = true;
}

void EventLoop::handle_send(std::uint64_t handle, int result, std::uint32_t flags) {
    TcpClient &client = tcp_clients_[handle::get_index(handle)];
    if (client.generation != handle::get_gen(handle)) {
        // stale request
        return;
    }

    client.send_pending = false;

    if (result < 0) {
        // TODO: deal with this properly
        do_tcp_close(handle, CloseType::Abort);
        return;
    }

    TcpClient::Send &write = client.send_queue.peek();

    // Partial write path
    if (static_cast<std::uint32_t>(result) < write.size) {
        write.buf += result;
        write.size -= result;

        if (client.closing) {
            if (!client.recv_pending && !client.close_sent) {
                ring_.sq_push_close(client.fd, handle::set_op(handle, handle::Op::TcpClose));
                client.close_sent = true;
            }
        } else {
            ring_.sq_push_send(client.fd, write.buf, std::min(write.size, MAX_WRITE_SIZE),
                               handle::set_op(handle, handle::Op::TcpSend));
            client.send_pending = true;
        }

        return;
    }

    // Full write
    client.on_send(TcpClientView(handle, *this), write.buf, write.size, client.context,
                   NetworkError::Ok);
    client.send_queue.pop();

    // Close may have been called in on_write
    if (client.closing) {
        if (!client.recv_pending && !client.close_sent) {
            ring_.sq_push_close(client.fd, handle::set_op(handle, handle::Op::TcpClose));
            client.close_sent = true;
        }
        return;
    }

    if (!client.send_queue.empty()) {
        auto [buf, size] = client.send_queue.peek();
        ring_.sq_push_send(client.fd, buf, size, handle::set_op(handle, handle::Op::TcpSend));
        client.send_pending = true;
        return;
    }

    if (!client.write_side_open) {
        // All pending writes are complete, so issue shutdown
        if (!client.read_side_open) {
            if (!client.close_sent) {
                ring_.sq_push_close(client.fd, handle::set_op(handle, handle::Op::TcpClose));
                client.close_sent = true;
            }
        } else if (!client.wr_shutdown_sent) {
            ring_.sq_push_shutdown(client.fd, SHUT_WR, handle::set_op(handle, handle::Op::TcpShutdownWr));
            client.wr_shutdown_sent = true;
        }

        return;
    }
}

void EventLoop::handle_shutdown_wr(std::uint64_t handle, int result, std::uint32_t flags) {
    TcpClient &client = tcp_clients_[handle::get_index(handle)];
    if (client.generation != handle::get_gen(handle))
        return;

    if (result < 0) {
        // TODO: handle error
    }
}

void EventLoop::handle_shutdown_rdwr(std::uint64_t handle, int result, std::uint32_t flags) {
    TcpClient &client = tcp_clients_[handle::get_index(handle)];
    if (client.generation != handle::get_gen(handle))
        return;

    if (result < 0) {
        // TODO: handle error
        return;
    }
}

void EventLoop::handle_close(std::uint64_t handle, int result, std::uint32_t flags) {
    TcpClient &client = tcp_clients_[handle::get_index(handle)];
    if (client.generation != handle::get_gen(handle))
        return;

    if (result < 0) {
        // TODO: handle error
        return;
    }

    while (!client.send_queue.empty()) {
        auto [buf, size] = client.send_queue.peek();
        client.on_send(TcpClientView(handle, *this), buf, size, client.context,
                       NetworkError::Closed);
        client.send_queue.pop();
    }

    client.on_close(TcpClientView(handle, *this), client.context, NetworkError::Ok);

    client.generation++;
    tcp_free_clients_.push_back(handle::get_index(handle));
    buffer_allocator_.free(client.read_buf);
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

    std::uint64_t handle = handle::make(thread_id_, index, tcp_listeners_[index].generation);
    listener = TcpListenerView(handle, *this);

    // post accept sqe
    ring_.sq_push_accept(fd, handle::set_op(handle, handle::Op::TcpAccept));
    on_listen(listener, err);
}

void EventLoop::do_tcp_connect(std::uint32_t host, std::uint16_t port, void *context,
                               OnConnect on_connect) {
    if (free_pending_connections_.empty()) {
        int size = pending_connections_.size();
        pending_connections_.resize(pending_connections_.size() * 2);
        for (int i = (size * 2) - 1; i >= size; i--) {
            pending_connections_[i] = std::make_unique<PendingConnection>();
            free_pending_connections_.push_back(i);
        }
    }

    int index = free_pending_connections_.back();
    free_pending_connections_.pop_back();

    pending_connections_[index]->context = context;
    pending_connections_[index]->on_connect = on_connect;
    pending_connections_[index]->sockaddr = sockaddr_in{
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = {.s_addr = htonl(host)},
    };

    ring_.sq_push_socket(AF_INET, SOCK_STREAM, 0,
                         handle::set_op(handle::make(thread_id_, index, 0), handle::Op::TcpSocket));
}

void EventLoop::do_tcp_send(std::uint64_t handle, std::uint8_t *buf, std::uint32_t size) {
    TcpClient &client = tcp_clients_[handle::get_index(handle)];

    if (client.generation != handle::get_gen(handle)) {
        // stale
        return;
    }

    if (client.closing || !client.write_side_open) {
        client.on_send(TcpClientView(handle, *this), buf, size, client.context,
                       NetworkError::Closed);
        return;
    }

    client.send_queue.push({.buf = buf, .size = size});
    if (!client.send_pending) {
        ring_.sq_push_send(client.fd, buf, std::min(size, MAX_WRITE_SIZE),
                           handle::set_op(handle, handle::Op::TcpSend));
        client.send_pending = true;
    }
}

void EventLoop::do_tcp_close(Handle handle, CloseType type) {
    TcpClient &client = tcp_clients_[handle::get_index(handle)];

    if (client.generation != handle::get_gen(handle)) {
        return;
    }

    if (client.close_sent) {
        return;
    }

    if (type == CloseType::Graceful) {
        // Close already underway
        if (!client.write_side_open || client.closing) {
            return;
        }

        client.write_side_open = false;

        if (!client.send_queue.empty()) {
            return;
        }

        if (client.read_side_open) {
            if (!client.wr_shutdown_sent) {
                ring_.sq_push_shutdown(client.fd, SHUT_WR,
                                       handle::set_op(handle, handle::Op::TcpShutdownWr));
                client.wr_shutdown_sent = true;
            }
        } else {
            // Both read and write sides are closed now, so close connection
            if (!client.close_sent) {
                ring_.sq_push_close(client.fd, handle::set_op(handle, handle::Op::TcpClose));
                client.close_sent = true;
            }
        }

    } else {
        client.closing = true;
        client.write_side_open = false;

        if (!client.recv_pending && !client.send_pending) {
            ring_.sq_push_close(client.fd, handle::set_op(handle, handle::Op::TcpClose));
            client.close_sent = true;
        } else if (!client.rdwr_shutdown_sent) {
            ring_.sq_push_shutdown(client.fd, SHUT_RDWR,
                                   handle::set_op(handle, handle::Op::TcpShutdownRdWr));
            client.rdwr_shutdown_sent = true;
        }
    }
}

void EventLoop::do_set_context(std::uint64_t handle, void *context) {
    TcpClient &client = tcp_clients_[handle::get_index(handle)];
    if (client.generation != handle::get_gen(handle)) {
        return;
    }
    client.context = context;
}

void EventLoop::do_set_onrecv(std::uint64_t handle, OnRecv on_read) {
    TcpClient &client = tcp_clients_[handle::get_index(handle)];
    if (client.generation != handle::get_gen(handle)) {
        return;
    }
    client.on_recv = on_read;
}

void EventLoop::do_set_onsend(std::uint64_t handle, OnSend on_write) {
    TcpClient &client = tcp_clients_[handle::get_index(handle)];
    if (client.generation != handle::get_gen(handle)) {
        return;
    }
    client.on_send = on_write;
}

void EventLoop::do_set_onclose(std::uint64_t handle, OnClose on_close) {
    TcpClient &client = tcp_clients_[handle::get_index(handle)];
    if (client.generation != handle::get_gen(handle)) {
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
                       handle::set_op(0, handle::Op::Wake));
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

    thread_loop = nullptr;
}

} // namespace wolf
