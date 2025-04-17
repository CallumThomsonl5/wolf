#ifndef WOLF_H_INCLUDED
#define WOLF_H_INCLUDED

#include <sys/eventfd.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace wolf {

/**
 * @brief Indicates error with listener
 */
class ListenerException : public std::exception {
public:
    ListenerException(const std::string &msg) : msg_(msg) {}
    const char *what() const noexcept override { return msg_.c_str(); }

private:
    const std::string msg_;
};

class TcpListener {
    using on_accept_cb_t = std::function<void()>;

public:
    TcpListener(std::uint32_t host, std::uint16_t port,
                on_accept_cb_t on_accept);
    ~TcpListener();

    int fd();

private:
    int fd_;
    std::uint32_t host_;
    std::uint16_t port_;
    on_accept_cb_t on_accept_cb_;
};

enum class MessageOperation : std::uint8_t { ATTACH_LISTENER };

struct Message {
    MessageOperation op;
    union {
        TcpListener *listener;
    } entity;
};

struct ThreadHandle {
    std::queue<Message> msg_queue;
    std::mutex mtx;
    int wake_fd;
};

/**
 * @brief The main class, handling the event loop.
 */
class EventLoop {
public:
    explicit EventLoop(int threads);
    ~EventLoop();

    void run();

    void attach(TcpListener &listener);

private:
    void thread_loop(int thread_id);

    bool is_running_ = false;
    std::mutex thread_start_mutex_;
    std::condition_variable thread_start_cv_;
    std::unique_ptr<ThreadHandle[]> thread_handles_;
    std::unique_ptr<std::thread[]> threads_;
    int threads_count_ = 0;
};

} // namespace wolf

#endif // WOLF_H_INCLUDED
