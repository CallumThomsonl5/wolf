#pragma once

#include <cstdint>
#include <span>

namespace wolf {

// Forward declarations
class EventLoop;
class TcpClientView;
class TcpListenerView;
enum class NetworkError;

using OnListen = void (*)(TcpListenerView, NetworkError err);
using OnAccept = void (*)(TcpClientView, NetworkError err);
using OnConnect = void (*)(TcpClientView, void *context, NetworkError err);
using OnRecv = void (*)(TcpClientView, std::span<uint8_t>, void *context,
                        NetworkError err);
using OnSend = void (*)(TcpClientView, std::span<const uint8_t>, void *context,
                        void *send_ctx, NetworkError err);
using OnTcpClose = void (*)(TcpClientView client, void *context, NetworkError err);

using Handle = uint64_t;

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

enum class CloseType { Abort, Graceful };

} // namespace wolf

