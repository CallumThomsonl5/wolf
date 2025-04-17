#include <arpa/inet.h>
#include <linux/io_uring.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/socket.h>

#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <variant>
#include <vector>

#include <iouring.h>
#include <wolf.h>

namespace wolf {

constexpr std::size_t MAX_DATA_TRANSFER = 65535;
constexpr std::uint32_t RING_ENTRIES_HINT = 1024;
constexpr std::uint32_t LISTEN_BACKLOG = 1024;

using WatchListItem = std::variant<TcpListener *>;

/**
 * @brief Creates a socket and starts listening.
 *
 * @throws ListenerException if creation fails.
 */
TcpListener::TcpListener(std::uint32_t host, std::uint16_t port,
                         on_accept_cb_t on_accept)
    : host_(host), port_(port), on_accept_cb_(std::move(on_accept)) {
    fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd_ < 0) {
        throw ListenerException(std::format(
            "Listener: failed to create socket. Got {}", strerror(errno)));
    }

    int val = 1;
    int err =
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(val));
    if (err < 0) {
        close(fd_);
        throw ListenerException(std::format(
            "Listener: failed to set SO_REUSEADDR. Got {}", strerror(errno)));
    }

    val = 1;
    err = setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, (void *)&val, sizeof(val));
    if (err < 0) {
        close(fd_);
        throw ListenerException(std::format(
            "Listener: failed to set SO_REUSEPORT. Got {}", strerror(errno)));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(host);
    addr.sin_port = htons(port);
    err = bind(fd_, (sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        close(fd_);
        throw ListenerException(
            std::format("Listener: failed to bind to {}:{}. Got {}", host, port,
                        strerror(errno)));
    }

    err = listen(fd_, LISTEN_BACKLOG);
    if (err < 0) {
        close(fd_);
        throw ListenerException(std::format(
            "Listener: failed to start listening. Got {}", strerror(errno)));
    }
}

/**
 * @brief Closes the associated file descriptor
 */
TcpListener::~TcpListener() { close(fd_); }

/**
 * @brief Returns the associated fd.
 */
int TcpListener::fd() { return fd_; }

/**
 * @brief Initialises the event loop threads
 *
 * @param threads Number of threads to use
 */
EventLoop::EventLoop(int threads)
    : thread_handles_(std::make_unique<ThreadHandle[]>(threads)),
      threads_(std::make_unique<std::thread[]>(threads)),
      threads_count_(threads) {

    for (int i = 0; i < threads; i++) {
        std::thread t(&EventLoop::thread_loop, this, i);
        threads_[i] = std::move(t);
        thread_handles_[i].wake_fd = eventfd(1, 0);
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

    for (int i = 0; i < threads_count_; i++) {
        threads_[i].join();
    }
}

/**
 * @brief Attaches a listner to the event loop so that it can begin accepting
 * connections.
 */
void EventLoop::attach(TcpListener &listener) {
    for (int i = 0; i < threads_count_; i++) {
        {
            std::lock_guard<std::mutex> l(thread_handles_[i].mtx);
            thread_handles_[i].msg_queue.push(
                {.op = MessageOperation::ATTACH_LISTENER,
                 .entity = {.listener = &listener}});
        }
        eventfd_write(thread_handles_[i].wake_fd, 1);
    }
}

/**
 * @brief Helper function to add an item to the watch list, making sure there is
 * enough space.
 */
void watch_list_push(std::vector<WatchListItem> &watch_list, WatchListItem item,
                     int fd) {
    if (watch_list.size() <= fd) {
        watch_list.resize(watch_list.size() + 1);
    }
    watch_list[fd] = std::move(item);
}

/**
 * @brief Loops through the message queue and handles all messages.
 */
static void handle_messages(IOUring &ring,
                            std::vector<WatchListItem> &watch_list,
                            ThreadHandle &thread_handle, int wake_fd,
                            std::uint64_t *wake_buf) {
    while (true) {
        Message msg;
        {
            std::lock_guard<std::mutex> l(thread_handle.mtx);
            if (thread_handle.msg_queue.empty())
                break;
            msg = thread_handle.msg_queue.front();
            thread_handle.msg_queue.pop();
        }

        switch (msg.op) {
        case MessageOperation::ATTACH_LISTENER:
            TcpListener *listener = msg.entity.listener;

            // Multishot kernel >= 6.0
            ring.sq_push(io_uring_sqe{
                .opcode = IORING_OP_ACCEPT,
                .fd = listener->fd(),
                .user_data = static_cast<std::uint64_t>(listener->fd())});
            watch_list_push(watch_list, WatchListItem(listener),
                            listener->fd());
            break;
        }
    }

    // rearm
    ring.sq_push({.opcode = IORING_OP_READ,
                  .fd = wake_fd,
                  .addr = std::bit_cast<std::uint64_t>(&wake_buf),
                  .len = sizeof(wake_buf),
                  .user_data = (std::uint64_t)wake_fd});
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
    ThreadHandle &this_handle = thread_handles_[thread_id];

    // Setup wake eventfd
    const int wake_fd = this_handle.wake_fd;
    std::uint64_t wake_buf;
    ring.sq_start_push();
    ring.sq_push({.opcode = IORING_OP_READ,
                  .fd = wake_fd,
                  .addr = std::bit_cast<std::uint64_t>(&wake_buf),
                  .len = sizeof(wake_buf),
                  .user_data = (std::uint64_t)wake_fd});
    ring.sq_end_push();

    while (is_running_) {
        int ret = ring.enter();
        if (ret < 0) {
            throw IOUringException(
                std::format("iouring: enter failed. Got {}", strerror(ret)));
        }

        ring.sq_start_push();
        ring.cq_start_pop();
        while (io_uring_cqe *cqe = ring.cq_pop()) {
            if (cqe->user_data == wake_fd) {
                handle_messages(ring, watch_list, this_handle, wake_fd,
                                &wake_buf);
                continue;
            }

            int fd = static_cast<int>(cqe->user_data);
            WatchListItem *item = &watch_list[fd];
            if (TcpListener **listener_ptr = std::get_if<TcpListener *>(item)) {
                TcpListener *listener = *listener_ptr;

                // TODO: handle accept cqe properly
                std::cout << std::format(
                    "accepted res: {}, user_data: {}, thread_id: {}\n",
                    cqe->res, cqe->user_data, thread_id);
            }
        }
        ring.cq_end_pop();
        ring.sq_end_push();
    }
}

} // namespace wolf
