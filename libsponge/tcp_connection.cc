#include "tcp_connection.hh"

#include "tcp_segment.hh"
#include "tcp_state.hh"

#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _now_time_ms - _seg_received_time_ms; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    _seg_received_time_ms = _now_time_ms;
    if (seg.header().rst) {
        set_rst_state();
        return;
    }
    // gives the segment to the TCPReceiver
    _receiver.segment_received(seg);
    // 如果当前tcp未收到syn报文段并且不是主动发起连接的syn_sent状态，直接返回
    if (auto receiver_state = TCPState::state_summary(_receiver), sender_state = TCPState::state_summary(_sender);
        receiver_state == TCPReceiverStateSummary::ERROR ||
        (receiver_state == TCPReceiverStateSummary::LISTEN && sender_state != TCPSenderStateSummary::SYN_SENT)) {
        return;
    }
    // inform the sender
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED) {
        _linger_after_streams_finish = false;
    }
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && !_linger_after_streams_finish) {
        _active = false;
    }
    if (seg.length_in_sequence_space() > 0) {
        if (!_sender.stream_in().buffer_empty() || TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
            _sender.fill_window();
        } else {
            // send at least one segment
            _sender.send_empty_segment();
        }
    } else if (_receiver.ackno().has_value() && seg.header().seqno == _receiver.ackno().value() - 1) {
        // reponding to keep-alive segment
        _sender.send_empty_segment();
    } else {
        _sender.fill_window();
    }
    send_with_ackno_and_win();
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t write_len = _sender.stream_in().write(data);
    _sender.fill_window();
    send_with_ackno_and_win();
    return write_len;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _now_time_ms += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);  // tick会重传sender超时的seg
    // 重传次数太多, 发送reset segment
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        // 之前的也别传了
        while (!_sender.segments_out().empty()) {
            _sender.segments_out().pop();
        }
        send_rst_seg();
        set_rst_state();
        // 这里不直接return了，因为后面的操作没有影响
    }
    send_with_ackno_and_win();
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && _linger_after_streams_finish &&
        _now_time_ms - _seg_received_time_ms >= 10 * _cfg.rt_timeout) {
        _active = false;
        // _linger_after_streams_finish = false;
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    // 发送fin, fill_window自己会根据_sender.stream_in().input_ended()设置FIN flag，直接fill_window()
    _sender.fill_window();
    send_with_ackno_and_win();
}

void TCPConnection::connect() {
    // _active = true;
    _sender.fill_window();
    send_with_ackno_and_win();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            send_rst_seg();
            set_rst_state();
            send_with_ackno_and_win();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::send_rst_seg() {
    // send a rst seg is similar to receive one
    TCPSegment tcp_seg;
    tcp_seg.header().seqno = _sender.next_seqno();
    tcp_seg.header().rst = true;
    _sender.segments_out().push(tcp_seg);
}

void TCPConnection::set_rst_state() {
    _active = false;
    _linger_after_streams_finish = false;
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
}

void TCPConnection::send_with_ackno_and_win() {
    while (!_sender.segments_out().empty()) {
        auto tmp_seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        tmp_seg.header().ack = TCPState::state_summary(_receiver) == TCPReceiverStateSummary::LISTEN ? false : true;
        tmp_seg.header().ackno = _receiver.ackno().value_or(WrappingInt32(0));
        tmp_seg.header().win = _receiver.window_size() > std::numeric_limits<uint16_t>::max()
                                   ? std::numeric_limits<uint16_t>::max()
                                   : _receiver.window_size();
        _segments_out.push(tmp_seg);
    }
}
