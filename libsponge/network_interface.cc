#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "ipv4_datagram.hh"
#include "parser.hh"

#include <algorithm>
#include <iostream>
#include <optional>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    // 如果有缓存，并且没有超时
    if (_cache_time.find(next_hop_ip) != _cache_time.end() &&
        _now_time_ms - _cache_time[next_hop_ip] < _arp_cache_timeout) {
        EthernetFrame ethernet_frame;
        ethernet_frame.header().type = EthernetHeader::TYPE_IPv4;
        ethernet_frame.header().src = _ethernet_address;
        ethernet_frame.header().dst = _ip_to_ethernet_cache[next_hop_ip];
        ethernet_frame.payload() = dgram.serialize();
        _frames_out.push(ethernet_frame);
        return;
    }
    if (_arp_request_his.find(next_hop_ip) == _arp_request_his.end() ||
        _now_time_ms - _arp_request_his[next_hop_ip] >= _arp_request_limit) {
        ARPMessage arp_req_msg;
        arp_req_msg.opcode = ARPMessage::OPCODE_REQUEST;
        arp_req_msg.sender_ip_address = _ip_address.ipv4_numeric();
        arp_req_msg.sender_ethernet_address = _ethernet_address;
        arp_req_msg.target_ip_address = next_hop_ip;
        // arp_req_msg.target_ethernet_address = ETHERNET_BROADCAST;
        EthernetFrame arp_req_ethernet_frame;
        arp_req_ethernet_frame.header().type = EthernetHeader::TYPE_ARP;
        arp_req_ethernet_frame.header().src = _ethernet_address;
        arp_req_ethernet_frame.header().dst = ETHERNET_BROADCAST;
        arp_req_ethernet_frame.payload() = arp_req_msg.serialize();
        _frames_out.push(arp_req_ethernet_frame);
        _arp_request_his[next_hop_ip] = _now_time_ms;
    }
    _unsent_datagram[next_hop_ip].emplace_back(dgram);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        IPv4Datagram ip_datagram;
        if (auto result = ip_datagram.parse(frame.payload());
            result == ParseResult::NoError && frame.header().dst == _ethernet_address) {
            return std::make_optional(ip_datagram);
        }
    } else if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_message;
        if (auto result = arp_message.parse(frame.payload()); result == ParseResult::NoError) {
            _cache_time[arp_message.sender_ip_address] = _now_time_ms;
            _ip_to_ethernet_cache[arp_message.sender_ip_address] = arp_message.sender_ethernet_address;
            for (auto &dgram : _unsent_datagram[arp_message.sender_ip_address]) {
                EthernetFrame ethernet_frame;
                ethernet_frame.header().type = EthernetHeader::TYPE_IPv4;
                ethernet_frame.header().src = _ethernet_address;
                ethernet_frame.header().dst = _ip_to_ethernet_cache[arp_message.sender_ip_address];
                ethernet_frame.payload() = dgram.serialize();
                _frames_out.push(ethernet_frame);
            }
            // 删除已经发送的datagram
            _unsent_datagram.erase(arp_message.sender_ip_address);
            // 如果是给本network_interface发送消息, 或者是广播
            if (arp_message.opcode == ARPMessage::OPCODE_REQUEST &&
                arp_message.target_ip_address == _ip_address.ipv4_numeric()) {
                ARPMessage arp_reply_msg;
                arp_reply_msg.opcode = ARPMessage::OPCODE_REPLY;
                arp_reply_msg.sender_ip_address = _ip_address.ipv4_numeric();
                arp_reply_msg.sender_ethernet_address = _ethernet_address;
                arp_reply_msg.target_ip_address = arp_message.sender_ip_address;
                arp_reply_msg.target_ethernet_address = arp_message.sender_ethernet_address;
                EthernetFrame arp_reply_ehthernet_frame;
                arp_reply_ehthernet_frame.header().type = EthernetHeader::TYPE_ARP;
                arp_reply_ehthernet_frame.header().src = _ethernet_address;
                arp_reply_ehthernet_frame.header().dst = arp_message.sender_ethernet_address;
                arp_reply_ehthernet_frame.payload() = arp_reply_msg.serialize();
                _frames_out.push(arp_reply_ehthernet_frame);
            }
        }
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _now_time_ms += ms_since_last_tick;
    // std::remove_if doesn't work for a map
    // std::remove_if(_ip_to_ethernet_cache.begin(), _ip_to_ethernet_cache.end(), [&](auto &[_, v]) {
    //     return _now_time_ms - v.second > 30000;
    // });
}
