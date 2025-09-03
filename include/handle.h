#pragma once
#include <cstdint>

namespace wolf::handle {

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

constexpr inline Handle make(int thread_id, int index, std::uint32_t gen) {
    return (std::uint64_t(thread_id) << THREAD_ID_SHIFT) | (std::uint64_t(index) << INDEX_SHIFT) |
           std::uint64_t(gen);
}

constexpr inline Handle set_op(Handle h, Op op) {
    return (h & ~OP_MASK) | (std::uint64_t(op) << OP_SHIFT);
}

constexpr inline Handle base(Handle h) { return h & ~OP_MASK; }

constexpr inline Op get_op(Handle h) { return Op(h >> OP_SHIFT); }
constexpr inline int get_thread(Handle h) { return int((h & THREAD_ID_MASK) >> THREAD_ID_SHIFT); }
constexpr inline int get_index(Handle h) { return int((h & INDEX_MASK) >> INDEX_SHIFT); }
constexpr inline std::uint32_t get_gen(Handle h) { return std::uint32_t(h); }

} // namespace wolf::handle
