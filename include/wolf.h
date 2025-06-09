#ifndef WOLF_H_INCLUDED
#define WOLF_H_INCLUDED

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <sys/eventfd.h>

#include <mpsc_queue.h>

namespace wolf {

using on_accept_t = void (*)();

enum class NetworkError {
    Ok,
    PermissionDenied,
    LimitReached,
    NoMemory,
    AddressInUse,
    Unknown
};

enum class MessageOperation : std::uint8_t { AttachListener };

struct Message {
    MessageOperation op;
    int fd;
    union {
        on_accept_t on_accept;
    } callback;
};

enum class WatchListItemType : std::uint8_t { Listener };

struct WatchListItem {
    WatchListItemType type;
    union {
        on_accept_t listener; // listener just on_accept callback for now
    } item;
};

/**
 * @brief The main class, handling the event loop.
 */
class EventLoop {
public:
    explicit EventLoop(int threads);
    ~EventLoop();

    void run();

    NetworkError tcp_listen(std::uint32_t host, std::uint16_t port,
                            on_accept_t on_accept);

private:
    void thread_loop(int thread_id);

    bool is_running_ = false;
    std::mutex thread_start_mutex_;
    std::condition_variable thread_start_cv_;
    std::vector<MPSCQueue<Message>> message_queues_;
    std::vector<std::thread> threads_;
};

inline std::uint32_t ipv4_address(std::uint8_t one, std::uint8_t two,
                                  std::uint8_t three, std::uint8_t four) {
    return (one << 24) | (two << 16) | (three << 8) | four;
}

} // namespace wolf

#endif // WOLF_H_INCLUDED
