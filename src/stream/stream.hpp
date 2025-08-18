#pragma once
#include "ssl_stream.hpp"
#include "variant_stream.hpp"

namespace libvnc {
using tcp_stream = boost::asio::ip::tcp::socket;
using ssl_tcp_stream = ssl_stream<tcp_stream>;

using vnc_stream_type = variant_stream<tcp_stream, ssl_tcp_stream>;
using vnc_stream_ptr = std::shared_ptr<vnc_stream_type>;
} // namespace libvnc