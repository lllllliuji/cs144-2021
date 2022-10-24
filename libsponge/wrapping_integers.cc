#include "wrapping_integers.hh"

#include <algorithm>
#include <cstdint>
#include <vector>

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint64_t diff = n + isn.raw_value();
    uint64_t mask = (1ULL << 32) - 1;
    return WrappingInt32{static_cast<uint32_t>(diff & mask)};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint32_t diff = n.raw_value() - isn.raw_value();
    constexpr uint64_t mask = ((1ULL << 32) - 1) << 32;
    std::vector<uint64_t> nums;
    nums.emplace_back((checkpoint & mask) | diff);
    nums.emplace_back(((checkpoint + (1ULL << 32)) & mask) | diff);
    nums.emplace_back(((checkpoint - (1ULL << 32)) & mask) | diff);
    return *std::min_element(nums.begin(), nums.end(), [&](uint64_t a, uint64_t b) {
        uint64_t dis_a = checkpoint > a ? checkpoint - a : a - checkpoint;
        uint64_t dis_b = checkpoint > b ? checkpoint - b : b - checkpoint;
        return dis_a < dis_b;
    });
}
