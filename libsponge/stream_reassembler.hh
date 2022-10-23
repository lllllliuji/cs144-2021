#ifndef SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
#define SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

#include "byte_stream.hh"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

//! \brief A class that assembles a series of excerpts from a byte stream (possibly out of order,
//! possibly overlapping) into an in-order byte stream.
class StreamReassembler {
  private:
    // Your code here -- add private members as necessary.
    using E = std::pair<size_t, std::string>;
    size_t _unassembled_bytes{};
    size_t _head_index{0};
    std::set<E> _auxiliary_cache{};
    bool _eof{false};

    ByteStream _output;  //!< The reassembled in-order byte stream
    size_t _capacity;    //!< The maximum number of bytes

    size_t merge(E &pa, const E &pb) {
        // 没有交集
        if (pa.first + pa.second.size() <= pb.first) {
            return 0;
        }
        // pa包含pb
        if (pa.first + pa.second.size() >= pb.first + pb.second.size()) {
            return pb.second.size();
        }
        // pa与pb有交集
        size_t start_index = pa.first + pa.second.size() - pb.first;
        pa.second += pb.second.substr(start_index);
        return start_index;
    }

    void assemble_string() {
        while (!_auxiliary_cache.empty()) {
            auto it = _auxiliary_cache.begin();
            if (it->first > _head_index) {
                break;
            }
            std::string str = it->second;
            size_t write_len = _output.write(str);
            _auxiliary_cache.erase(it);
            _unassembled_bytes -= str.size();
            _head_index += write_len;
            // 没有必要，因为插入的时候已经检测有足够的空间了
            // 如果当前没有足够的空间
            // if (write_len != str.size()) {
            //     auto node = std::make_pair(_head_index, str.substr(write_len));
            //     _auxiliary_cache.insert(node);
            //     break;
            // }
        }
    }

  public:
    //! \brief Construct a `StreamReassembler` that will store up to `capacity` bytes.
    //! \note This capacity limits both the bytes that have been reassembled,
    //! and those that have not yet been reassembled.
    StreamReassembler(const size_t capacity);

    //! \brief Receive a substring and write any newly contiguous bytes into the stream.
    //!
    //! The StreamReassembler will stay within the memory limits of the `capacity`.
    //! Bytes that would exceed the capacity are silently discarded.
    //!
    //! \param data the substring
    //! \param index indicates the index (place in sequence) of the first byte in `data`
    //! \param eof the last byte of `data` will be the last byte in the entire stream
    void push_substring(const std::string &data, const uint64_t index, const bool eof);

    //! \name Access the reassembled byte stream
    //!@{
    const ByteStream &stream_out() const { return _output; }
    ByteStream &stream_out() { return _output; }
    //!@}

    //! The number of bytes in the substrings stored but not yet reassembled
    //!
    //! \note If the byte at a particular index has been pushed more than once, it
    //! should only be counted once for the purpose of this function.
    size_t unassembled_bytes() const;

    //! \brief Is the internal state empty (other than the output stream)?
    //! \returns `true` if no substrings are waiting to be assembled
    bool empty() const;
};

#endif  // SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
