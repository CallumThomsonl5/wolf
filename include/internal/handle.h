#pragma once
#include <cstdint>
#include <format>
#include <string>

namespace wolf::internal {

/* handle format: Op(4)|THREAD_ID(6)|INDEX(22)|GENERATION(32) */
using Handle = std::uint64_t;

enum class Op : std::uint8_t {
    TcpAccept,
    TcpSocket,
    TcpConnect,
    TcpSend,
    TcpRecv,
    TcpShutdownWr,
    TcpShutdownRdWr,
    TcpClose,
    Timer,
    FileOpen,
    FileRead,
    FileWrite,
    Wake,
};

constexpr int OP_BITS = 4;
constexpr int THREAD_ID_BITS = 6;
constexpr int INDEX_BITS = 22;
constexpr int GENERATION_BITS = 32;

static_assert(OP_BITS + THREAD_ID_BITS + INDEX_BITS + GENERATION_BITS == 64);

constexpr int OP_SHIFT = GENERATION_BITS + INDEX_BITS + THREAD_ID_BITS;
constexpr int THREAD_ID_SHIFT = GENERATION_BITS + INDEX_BITS;
constexpr int INDEX_SHIFT = GENERATION_BITS;

constexpr std::uint64_t OP_MASK = ((1ULL << OP_BITS) - 1) << OP_SHIFT;
constexpr std::uint64_t THREAD_ID_MASK = ((1ULL << THREAD_ID_BITS) - 1) << THREAD_ID_SHIFT;
constexpr std::uint64_t INDEX_MASK = ((1ULL << INDEX_BITS) - 1) << INDEX_SHIFT;

constexpr inline Handle make_handle(int thread_id, int index, std::uint32_t gen) {
    return (std::uint64_t(thread_id) << THREAD_ID_SHIFT) | (std::uint64_t(index) << INDEX_SHIFT) |
           std::uint64_t(gen);
}

constexpr inline Handle set_op(Handle h, Op op) {
    return (h & ~OP_MASK) | (std::uint64_t(op) << OP_SHIFT);
}

constexpr inline Handle clear_op(Handle h) { return h & (~OP_MASK); }

constexpr inline Op get_op(Handle h) { return Op(h >> OP_SHIFT); }
constexpr inline int get_thread(Handle h) { return int((h & THREAD_ID_MASK) >> THREAD_ID_SHIFT); }
constexpr inline int get_index(Handle h) { return int((h & INDEX_MASK) >> INDEX_SHIFT); }
constexpr inline std::uint32_t get_gen(Handle h) { return std::uint32_t(h); }

inline std::string handle_to_string(Handle h) {
    auto op_to_string = [](Op op) {
        switch (op) {
        case Op::TcpAccept:
            return "TcpAccept";
        case Op::TcpSocket:
            return "TcpSocket";
        case Op::TcpConnect:
            return "TcpConnect";
        case Op::TcpSend:
            return "TcpSend";
        case Op::TcpRecv:
            return "TcpRecv";
        case Op::TcpShutdownWr:
            return "TcpShutdownWr";
        case Op::TcpShutdownRdWr:
            return "TcpShutdownRdWr";
        case Op::TcpClose:
            return "TcpClose";
        case Op::Timer:
            return "Timer";
        case Op::FileOpen:
            return "FileOpen";
        case Op::FileRead:
            return "FileRead";
        case Op::FileWrite:
            return "FileWrite";
        case Op::Wake:
            return "Wake";
        default:
            return "Invalid OP";
        }
    };

    return std::format("op={}, thread={}, index={}, gen={}", op_to_string(get_op(h)), get_thread(h),
                       get_index(h), get_gen(h));
}

} // namespace wolf::internal
