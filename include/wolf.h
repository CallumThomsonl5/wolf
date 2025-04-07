#ifndef WOLF_H_INCLUDED
#define WOLF_H_INCLUDED

namespace wolf {

// Forward declerations
struct TcpListener;

/**
 * @brief The main class, handling the event loop.
 */
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    void run();

    void attach(TcpListener &listener);

private:
    void poll_io(int timeout);

    bool is_running_ = false;
    int epollfd_ = -1;
};

} // namespace wolf

#endif // WOLF_H_INCLUDED
