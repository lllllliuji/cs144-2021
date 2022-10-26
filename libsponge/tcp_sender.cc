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
    , _retransmission_timeout(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const {
    return _next_seqno - _latest_ackno.raw_value();
    // if (_tcp_seg_cache.empty()) {
    //     return 0;
    // }
    // auto it = std::find_if(_tcp_seg_cache.begin(), _tcp_seg_cache.end(), [&](const auto &item) {
    //     WrappingInt32 right_end_seqno =
    //         item.second->header().seqno + static_cast<uint32_t>(item.second->length_in_sequence_space());
    //     return compare(right_end_seqno, _latest_ackno, _isn, _next_seqno);
    // });
    // if (it == _tcp_seg_cache.end()) {
    //     return 0;
    // }
    // uint64_t bytes_in_flight = 0UL;
    // uint64_t start_absolute_seqno = unwrap((*it).second->header().seqno, _isn, _next_seqno);
    // uint64_t end_absoulte_seqno = unwrap(_tcp_seg_cache.back().second->header().seqno +
    //                                          static_cast<uint32_t>(_tcp_seg_cache.back().second->payload().size()),
    //                                      _isn,
    //                                      _next_seqno);
    // bytes_in_flight = end_absoulte_seqno - start_absolute_seqno;
    // std::cout << "bytes in flight: " << bytes_in_flight << std::endl;
    // return bytes_in_flight;
}

void TCPSender::fill_window() {
    size_t read_size = std::min(_window_size, TCPConfig::MAX_PAYLOAD_SIZE);
    auto content = _stream.read(read_size);
    int64_t content_size = content.size();
    // 如果同步过，并且没有内容，不发送空segment
    if (_syned && content_size == 0) {
        return;
    }
    TCPSegment tcp_seg;
    tcp_seg.payload() = std::move(content);
    tcp_seg.header().seqno = wrap(_next_seqno, _isn);
    tcp_seg.header().syn = _syned == true ? false : (_syned = true);
    tcp_seg.header().fin = _stream.eof();
    _next_seqno += content_size + tcp_seg.header().syn + tcp_seg.header().fin;
    _segments_out.push(tcp_seg);
    std::unique_ptr<TCPSegment> tcp_seg_copy = std::make_unique<TCPSegment>(tcp_seg);
    _tcp_seg_cache.push_back(std::make_pair(_now_time_ms, std::move(tcp_seg_copy)));
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 如果ackno没有_latest_ackno新，直接返回
    if (ackno == _latest_ackno || compare(_latest_ackno, ackno, _isn, _next_seqno) ||
        unwrap(ackno, _isn, _next_seqno) >= _next_seqno) {
        return;
    }
    _window_size = window_size == 0 ? 1 : window_size;
    _latest_ackno = ackno;
    // 删除已经ack的segment
    remove_acked_seg();
    // 重传
    std::for_each(_tcp_seg_cache.begin(), _tcp_seg_cache.end(), [&](auto &item) {
        if (_now_time_ms - item.first >= _initial_retransmission_timeout) {
            item.first = _now_time_ms;
            _segments_out.push(*item.second);
        }
    });
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { _now_time_ms += ms_since_last_tick; }

unsigned int TCPSender::consecutive_retransmissions() const {
    unsigned int ret = 0;
    std::for_each(_tcp_seg_cache.begin(), _tcp_seg_cache.end(), [&](const auto &item) {
        if (_now_time_ms - item.first >= _retransmission_timeout) {
            ret++;
        }
    });
    return ret;
}

void TCPSender::send_empty_segment() {
    TCPSegment empty_seg;
    empty_seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(empty_seg);
}
