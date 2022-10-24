#include "tcp_receiver.hh"

#include "wrapping_integers.hh"

#include <iostream>
#include <optional>
#include <string>
#include <utility>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    auto header = seg.header();
    auto payload = seg.payload();
    // 如果没有设置过 isn，并且本次数据也不是同步信息，直接返回
    if (!_set_isn && !header.syn) {
        return;
    }
    // 第一次同步
    uint64_t bias = 1;
    if (!_set_isn) {
        _isn = header.seqno;
        _set_isn = header.syn;
        bias = 0;
    }
    uint64_t checkpoint = _reassembler.stream_out().bytes_written();
    uint64_t absolute_seqno = unwrap(header.seqno, _isn, checkpoint);
    // 怎么计算出 stream index ?
    _reassembler.push_substring(payload.copy(), absolute_seqno - bias, header.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_set_isn) {
        return {};
    }
    auto bytes_written = _reassembler.stream_out().bytes_written();
    auto seq_count = _set_isn + bytes_written + _reassembler.stream_out().input_ended();
    return std::make_optional<WrappingInt32>(wrap(seq_count, _isn));
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
