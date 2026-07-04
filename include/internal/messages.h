#pragma once

#include <files.h>
#include <types.h>

#include <cstdint>

namespace wolf::internal {

enum class MessageType : uint8_t {
    CreateListener,
    TcpConnect,
    TcpSend,
    TcpClose,
    SetContext,
    SetOnRecv,
    SetOnSend,
    SetOnClose,
    FileOpen,
    FileIO,
    FileSetOnRead
};

struct CreateListenerMessage {
    uint32_t host;
    uint16_t port;
    OnListen on_listen;
    OnAccept on_accept;
};

struct ConnectMessage {
    uint32_t host;
    uint16_t port;
    void *context;
    OnConnect on_connect;
};

struct SendMessage {
    void *context;
    uint8_t *buf;
    uint64_t handle;
    uint32_t size;
};

struct CloseMessage {
    CloseType type;
    uint64_t handle;
};

struct SetContextMessage {
    void *context;
    uint64_t handle;
};

struct SetOnRecvMessage {
    OnRecv on_read;
    uint64_t handle;
};

struct SetOnSendMessage {
    OnSend on_write;
    uint64_t handle;
};

struct SetOnCloseMessage {
    OnTcpClose on_close;
    uint64_t handle;
};

struct FileOpenMessage {
    const char *path;
    FileOpenMode mode;
    FileOpenOptions options;
    int perms;
    void *context;
    OnOpen on_open;
};

struct FileIOMessage {
    enum class Type {
        Read,
        Write
    } type;
    size_t off;
    uint8_t *buf;
    size_t len;
    uint64_t token;
    uint64_t handle;
};

struct FileSetOnRead {
    OnRead on_read;
    uint64_t handle;
};

/**
 * @internal
 * @brief Used for passing messages between event loops.
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
        FileIOMessage file_io;
        FileSetOnRead file_set_onread;
    } msg;
    MessageType type;
};

} // namespace wolf::internal

