#include "tcp_sender.hh"

#include "buffer.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <sys/types.h>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _retransmission_timeout(retx_timeout)
    , _latest_ackno(_isn) {}

uint64_t TCPSender::bytes_in_flight() const {
    // std::cout << "next seqno: " << _next_seqno << " latest ackno: " << unwrap(_latest_ackno, _isn, _next_seqno) <<
    // std::endl;
    return _next_seqno - unwrap(_latest_ackno, _isn, _next_seqno);
}

void TCPSender::fill_window() {
    size_t read_size = std::min(_window_size, TCPConfig::MAX_PAYLOAD_SIZE);
    // 读取窗口最小为 1
    read_size = std::max(read_size, 1UL);
    auto content = _stream.read(read_size);
    TCPSegment tcp_seg;
    tcp_seg.payload() = std::move(content);
    tcp_seg.header().seqno = wrap(_next_seqno, _isn);
    tcp_seg.header().syn = _syned == false ? (_syned = true) : false;
    tcp_seg.header().fin = _fin == false ? (_fin = _stream.input_ended()) : false;
    // 如果payload的总的sequence数量为0， 不发送segment
    uint64_t total_size = tcp_seg.payload().size() + tcp_seg.header().syn + tcp_seg.header().fin;
    if (total_size == 0) {
        return;
    }
    _next_seqno += total_size;
    _segments_out.push(tcp_seg);
    std::unique_ptr<TCPSegment> tcp_seg_copy = std::make_unique<TCPSegment>(tcp_seg);
    _tcp_seg_cache.push_back(std::make_pair(_now_time_ms, std::move(tcp_seg_copy)));
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 如果ackno没有_latest_ackno新，或者超出了绝对序列，直接返回
    if (ackno == _latest_ackno || compare(_latest_ackno, ackno, _isn, _next_seqno) ||
        unwrap(ackno, _isn, _next_seqno) > _next_seqno) {
        return;
    }
    _window_size = window_size;
    _latest_ackno = ackno;
    _retransmission_timeout = _initial_retransmission_timeout;
    _retransmission_count = 0;
    remove_acked_seg();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _now_time_ms += ms_since_last_tick;
    // 删除已经ack的segment
    remove_acked_seg();
    // 重传超时，最早的seg
    auto it = std::find_if(_tcp_seg_cache.begin(), _tcp_seg_cache.end(), [&](const auto &item) {
        return _now_time_ms - item.first >= _retransmission_timeout;
    });
    if (it != _tcp_seg_cache.end()) {
        it->first = _now_time_ms;
        _segments_out.push(*(it->second));
        if (_window_size != 0) {
            _retransmission_count++;
            _retransmission_timeout *= 2;
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _retransmission_count; }

void TCPSender::send_empty_segment() {
    TCPSegment empty_seg;
    empty_seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(empty_seg);
}
