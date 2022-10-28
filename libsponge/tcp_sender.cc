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
// void TCPSender::send_seg(uint64_t payload_size) {
//     uint64_t window_unused_size = _window_size > _window_used_size ? _window_size - _window_used_size : 0;
//     bool is_syn_seg = _syned == false ? (_syned = true) : false;
//     bool is_fin_seg = false;
//     auto content = _stream.read(payload_size);
//     // 查看有没有空间存放fin seqno
//     if (_stream.input_ended() && !_fin && content.size() + is_syn_seg < window_unused_size) {
//         _fin = true;
//         is_fin_seg = true;
//     }
//     TCPSegment tcp_seg;
//     tcp_seg.payload() = std::move(content);
//     tcp_seg.header().seqno = wrap(_next_seqno, _isn);
//     tcp_seg.header().syn = is_syn_seg;
//     tcp_seg.header().fin = is_fin_seg;
//     // 如果payload的总的sequence数量为0， 不发送segment
//     uint64_t total_size = tcp_seg.length_in_sequence_space();
//     if (total_size == 0) {
//         return;
//     }
//     _window_used_size += total_size;
//     _next_seqno += total_size;
//     _segments_out.push(tcp_seg);
//     std::unique_ptr<TCPSegment> tcp_seg_copy = std::make_unique<TCPSegment>(tcp_seg);
//     _tcp_seg_cache.push_back(std::make_pair(_now_time_ms, std::move(tcp_seg_copy)));
// }
void TCPSender::fill_window() {
    if (_window_size == 0 && _window_used_size == 0) {
        TCPSegment tcp_seg;
        if (!_stream.buffer_empty()) {
            auto content = _stream.read(1);
            tcp_seg.payload() = std::move(content);
        } else if (_stream.input_ended() && !_fin) {
            _fin = true;
            tcp_seg.header().fin = true;
        }
        tcp_seg.header().seqno = wrap(_next_seqno, _isn);
        _next_seqno += 1;
        _window_used_size++;
        _segments_out.push(tcp_seg);
        std::unique_ptr<TCPSegment> tcp_seg_copy = std::make_unique<TCPSegment>(tcp_seg);
        _tcp_seg_cache.push_back(std::make_pair(_now_time_ms, std::move(tcp_seg_copy)));
        return;
    }
    while (!_stream.buffer_empty() || !_syned || !_fin) {
        uint64_t window_unused_size = _window_size > _window_used_size ? _window_size - _window_used_size : 0;
        if (window_unused_size == 0) {
            return;
        }
        bool is_syn_seg = _syned == false ? (_syned = true) : false;
        bool is_fin_seg = false;
        // 读取窗口最小为 1
        uint64_t payload_size = std::max(1UL, std::min(window_unused_size - is_syn_seg, TCPConfig::MAX_PAYLOAD_SIZE));
        auto content = _stream.read(payload_size);
        // 查看有没有空间存放fin seqno
        if (_stream.input_ended() && !_fin && content.size() + is_syn_seg < window_unused_size) {
            _fin = true;
            is_fin_seg = true;
        }
        TCPSegment tcp_seg;
        tcp_seg.payload() = std::move(content);
        tcp_seg.header().seqno = wrap(_next_seqno, _isn);
        tcp_seg.header().syn = is_syn_seg;
        tcp_seg.header().fin = is_fin_seg;
        // 如果payload的总的sequence数量为0， 不发送segment
        uint64_t total_size = tcp_seg.length_in_sequence_space();
        if (total_size == 0) {
            return;
        }
        _window_used_size += total_size;
        _next_seqno += total_size;
        _segments_out.push(tcp_seg);
        std::unique_ptr<TCPSegment> tcp_seg_copy = std::make_unique<TCPSegment>(tcp_seg);
        _tcp_seg_cache.push_back(std::make_pair(_now_time_ms, std::move(tcp_seg_copy)));
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 如果ackno没有_latest_ackno新，或者超出了绝对序列，直接返回
    if (compare(_latest_ackno, ackno, _isn, _next_seqno) || unwrap(ackno, _isn, _next_seqno) > _next_seqno) {
        return;
    }
    _window_size = window_size;
    uint64_t a = unwrap(_latest_ackno + _window_used_size, _isn, _next_seqno);
    uint64_t b = unwrap(ackno, _isn, _next_seqno);
    // 求差值
    _window_used_size = a > b ? a - b : 0;
    _window_used_size = _window_used_size > window_size ? window_size : _window_used_size;
    if (ackno == _latest_ackno) {
        return;
    }
    _retransmission_timeout = _initial_retransmission_timeout;
    _retransmission_count = 0;
    _latest_ackno = ackno;
    remove_acked_seg();
    std::for_each(_tcp_seg_cache.begin(), _tcp_seg_cache.end(), [&](auto &item) { item.first = _now_time_ms; });
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _now_time_ms += ms_since_last_tick;
    // 删除已经ack的segment
    remove_acked_seg();
    // 重传超时，最早的seg
    // auto it = std::find_if(_tcp_seg_cache.begin(), _tcp_seg_cache.end(), [&](const auto &item) {
    //     return _now_time_ms - item.first >= _retransmission_timeout;
    // });
    if (_tcp_seg_cache.empty()) {
        return;
    }
    auto it = _tcp_seg_cache.begin();
    if (_now_time_ms - it->first >= _retransmission_timeout) {
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
