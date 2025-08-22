#pragma once
#include "ssl_stream.hpp"
#include "variant_stream.hpp"

namespace libvnc {
using tcp_stream = boost::asio::ip::tcp::socket;
using ssl_tcp_stream = ssl_stream<tcp_stream>;

using socket_stream = variant_stream<tcp_stream, ssl_tcp_stream>;
using socket_stream_ptr = std::shared_ptr<socket_stream>;
}  // namespace libvnc