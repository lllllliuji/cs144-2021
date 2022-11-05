#include "router.hh"

#include "address.hh"

#include <cstdint>
#include <iostream>
#include <optional>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    DUMMY_CODE(route_prefix, prefix_length, next_hop, interface_num);
    // Your code here.
    _route_table.emplace_back(
        route_prefix, prefix_length, next_hop.has_value() ? next_hop.value().ipv4_numeric() : 0, interface_num);
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    DUMMY_CODE(dgram);
    // Your code here.
    // time to live hits zero
    if (dgram.header().ttl == 0 || dgram.header().ttl == 1) {
        return;
    }
    dgram.header().ttl--;
    auto best_match_iter = _route_table.end();
    uint32_t dst_ip = dgram.header().dst;
    uint8_t max_match_len = 0;
    bool flag = false;
    // std::cout << dst_ip << " " << std::endl;
    for (auto it = _route_table.begin(); it != _route_table.end(); it++) {
        uint32_t route_prefix = (*it).route_prefix;
        uint8_t prefix_length = (*it).prefix_length;
        uint8_t match_len = 0;
        for (size_t i = 0; i <= 32; i++) {
            if ((dst_ip >> i) == (route_prefix >> i)) {
                match_len = 32 - i;
                break;
            }
        }
        if (match_len >= prefix_length && prefix_length >= max_match_len) {
            max_match_len = prefix_length;
            best_match_iter = it;
            flag = true;
            // std::cout << (*it).route_prefix << " " << (*it).prefix_length << std::endl;
        }
    }
    if (flag) {
        auto interface_num = (*best_match_iter).interface_num;
        auto next_hop = (*best_match_iter).next_hop;
        // RouteMsg.Optional == empty
        if (next_hop == 0) {
            _interfaces[interface_num].send_datagram(dgram, Address::from_ipv4_numeric(dst_ip));
        } else {
            _interfaces[interface_num].send_datagram(dgram, Address::from_ipv4_numeric(next_hop));
        }
    }
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
