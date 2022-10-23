#include "stream_reassembler.hh"

#include <netinet/in.h>
#include <utility>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (index + data.size() <= _head_index + _output.remaining_capacity()) {
        _eof |= eof;
    }
    // 超出范围，或者已经有序
    if (index >= _head_index + _output.remaining_capacity() || index + data.size() <= _head_index) {
        if (_eof && empty()) {
            _output.end_input();
        }
        return;
    }
    size_t start_index = std::max(index, _head_index);
    size_t write_len = std::min(_head_index + _output.remaining_capacity(), index + data.size()) - start_index;
    E node = std::make_pair(start_index, data.substr(start_index - index, write_len));
    // 合并后面
    while (true) {
        auto it = _auxiliary_cache.lower_bound(node);
        if (it == _auxiliary_cache.end()) {
            break;
        }
        if (it->first + it->second.size() <= start_index + write_len) {
            _unassembled_bytes -= it->second.size();
            _auxiliary_cache.erase(it);
        } else {
            auto overlap_size = merge(node, *it);
            if (overlap_size != 0) {
                _auxiliary_cache.erase(it);
            }
            _unassembled_bytes -= overlap_size;
            break;
        }
    }
    // 合并前面
    auto it = _auxiliary_cache.lower_bound(node);
    if (it != _auxiliary_cache.begin()) {
        auto pre_iter = --it;
        auto pre_node = *pre_iter;
        auto overlap_size = merge(pre_node, node);
        if (overlap_size == 0) {
            _auxiliary_cache.insert(node);
        } else {
            _auxiliary_cache.erase(pre_iter);
            _auxiliary_cache.insert(pre_node);
        }
        _unassembled_bytes += write_len;
        _unassembled_bytes -= overlap_size;
    } else {
        _auxiliary_cache.insert(node);
        _unassembled_bytes += write_len;
    }
    // reorder
    assemble_string();
    if (empty() && _eof) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return _unassembled_bytes == 0; }
