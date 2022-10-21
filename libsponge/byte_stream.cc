#include "byte_stream.hh"

#include <algorithm>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : buf(capacity, ' '), _capacity(capacity) {}

size_t ByteStream::write(const string &data) {
    if (_input_ended) {
        return 0;
    }
    size_t remain_capcity = remaining_capacity();
    size_t write_length = min(remain_capcity, data.size());
    for (size_t i = 0; i < write_length; i++) {
        buf[_front] = data[i];
        _front = (_front + 1 == _capacity) ? 0 : _front + 1;
    }
    _cur_size += write_length;
    _write_count += write_length;
    return write_length;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t peek_length = std::min(_cur_size, len);
    std::string ret(peek_length, ' ');
    for (size_t i = 0, start = _rear; i < peek_length; i++) {
        ret[i] = buf[start];
        start = (start + 1 == _capacity) ? 0 : start + 1;
    }
    return ret;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t pop_length = std::min(_cur_size, len);
    _rear = (_rear + pop_length) % _capacity;
    _cur_size -= pop_length;
    _read_count += pop_length;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    std::string ret = peek_output(len);
    pop_output(len);
    return ret;
}

void ByteStream::end_input() { _input_ended = true; }

bool ByteStream::input_ended() const { return _input_ended; }

size_t ByteStream::buffer_size() const { return _cur_size; }

bool ByteStream::buffer_empty() const { return _cur_size == 0; }

bool ByteStream::eof() const { return _input_ended && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _write_count; }

size_t ByteStream::bytes_read() const { return _read_count; }

size_t ByteStream::remaining_capacity() const { return _capacity - _cur_size; }
