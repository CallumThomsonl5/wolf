#include <wolf.h>

#include <cerrno>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <arpa/inet.h>
#include <linux/io_uring.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/socket.h>

#include <iouring.h>
#include <mpsc_queue.h>

namespace wolf {

static constexpr std::size_t MAX_DATA_TRANSFER = 65535;
static constexpr std::uint32_t RING_ENTRIES_HINT = 1024;
static constexpr std::uint32_t LISTEN_BACKLOG = 4096;

/**
 * @brief Initialises the event loop threads
 *
 * @param threads Number of threads to use
 */
EventLoop::EventLoop(int threads) {
    for (int i = 0; i < threads; i++) {
        message_queues_.emplace_back(eventfd(1, 0));
        threads_.emplace_back(&EventLoop::thread_loop, this, i);
    }
}

EventLoop::~EventLoop() = default;

/**
 * @brief Starts the event loop by signalling all threads to run.
 */
void EventLoop::run() {
    {
        std::lock_guard start_lock(thread_start_mutex_);
        is_running_ = true;
    }
    thread_start_cv_.notify_all();

    for (int i = 0; i < threads_.size(); i++) {
        threads_[i].join();
    }
}

static NetworkError create_listening_socket(std::uint32_t host,
                                            std::uint16_t port,
                                            int &socket_fd) {
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
    int err =
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(val));
    if (err != 0) {
        close(fd);
        return NetworkError::Unknown;
    }

    err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (void *)&val, sizeof(val));
    if (err != 0) {
        close(fd);
        return NetworkError::Unknown;
    }

    sockaddr_in addr{.sin_family = AF_INET,
                     .sin_port = htons(port),
                     .sin_addr = {.s_addr = htonl(host)}};
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

/**
 * @brief Creates a socket and starts listening.
 */
NetworkError EventLoop::tcp_listen(std::uint32_t host, std::uint16_t port,
                                   on_accept_t on_accept) {
    std::vector<int> sockets;
    for (int i = 0; i < threads_.size(); i++) {
        int fd;
        NetworkError err = create_listening_socket(host, port, fd);
        if (err != NetworkError::Ok) {
            for (int f : sockets) {
                close(f);
            }
            return err;
        }
        sockets.push_back(fd);
    }

    for (int i = 0; i < threads_.size(); i++) {
        message_queues_[i].push({.op = MessageOperation::AttachListener,
                                 .fd = sockets[i],
                                 .callback = {.on_accept = on_accept}});
    }
    return NetworkError::Ok;
}

/**
 * @brief Helper function to add an item to the watch list, making sure there is
 * enough space.
 */
void watch_list_push(std::vector<WatchListItem> &watch_list, WatchListItem item,
                     int fd) {
    if (watch_list.size() <= fd) {
        watch_list.resize(fd + 1);
    }
    watch_list[fd] = item;
}

/**
 * @brief Loops through the message queue and handles all messages.
 */
static void handle_messages(IOUring &ring,
                            std::vector<WatchListItem> &watch_list,
                            MPSCQueue<Message> &message_queue,
                            const std::uint64_t *wake_buf) {
    std::vector<Message> messages = message_queue.drain();
    for (Message &msg : messages) {
        switch (msg.op) {
        case MessageOperation::AttachListener:
            // Multishot kernel >= 6.0
            ring.sq_push(
                io_uring_sqe{.opcode = IORING_OP_ACCEPT,
                             .ioprio = IORING_ACCEPT_MULTISHOT,
                             .fd = msg.fd,
                             .user_data = static_cast<std::uint64_t>(msg.fd)});

            watch_list_push(
                watch_list,
                WatchListItem{.type = WatchListItemType::Listener,
                              .item = {.listener = msg.callback.on_accept}},
                msg.fd);
            break;
        }
    }

    // rearm
    ring.sq_push({.opcode = IORING_OP_READ,
                  .fd = message_queue.wake_fd(),
                  .addr = std::bit_cast<std::uint64_t>(&wake_buf),
                  .len = sizeof(wake_buf),
                  .user_data = (std::uint64_t)message_queue.wake_fd()});
}

/**
 * @brief The main loop run by each worker thread.
 */
void EventLoop::thread_loop(int thread_id) {
    {
        std::unique_lock start_lock(thread_start_mutex_);
        thread_start_cv_.wait(start_lock, [&] { return is_running_; });
    }

    IOUring ring(RING_ENTRIES_HINT);
    std::vector<WatchListItem> watch_list;
    MPSCQueue<Message> &message_queue = message_queues_[thread_id];

    // Setup wake eventfd
    std::uint64_t wake_buf;
    ring.sq_start_push();
    ring.sq_push({.opcode = IORING_OP_READ,
                  .fd = message_queue.wake_fd(),
                  .addr = std::bit_cast<std::uint64_t>(&wake_buf),
                  .len = sizeof(wake_buf),
                  .user_data = (std::uint64_t)message_queue.wake_fd()});
    ring.sq_end_push();

    while (is_running_) {
        int ret = ring.enter();
        if (ret < 0) {
            throw std::runtime_error(
                std::format("iouring: enter failed. Got {}", strerror(ret)));
        }

        ring.sq_start_push();
        ring.cq_start_pop();
        while (io_uring_cqe *cqe = ring.cq_pop()) {
            if (cqe->user_data == message_queue.wake_fd()) {
                handle_messages(ring, watch_list, message_queue, &wake_buf);
                continue;
            }

            int fd = static_cast<int>(cqe->user_data);
            WatchListItem &item = watch_list[fd];
            if (item.type == WatchListItemType::Listener) {
                item.item.listener();

                // TODO: handle accept cqe properly
                if (cqe->res < 0) {
                    throw std::runtime_error(std::format(
                        "got err accepting: {}", strerror(-cqe->res)));
                }
            }
        }
        ring.cq_end_pop();
        ring.sq_end_push();
    }
}

} // namespace wolf
