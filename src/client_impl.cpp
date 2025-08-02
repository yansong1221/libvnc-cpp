#include "client_impl.h"

#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <spdlog/spdlog.h>

namespace libvnc {
client_impl::client_impl(const boost::asio::any_io_executor& executor)
    : socket_(executor)
{
}

boost::asio::awaitable<boost::system::error_code>
client_impl::connect(const boost::asio::ip::tcp::endpoint& endp)
{
    if (socket_.is_open()) {
        boost::system::error_code ec;
        socket_.close(ec);
    }
    boost::system::error_code ec;
    socket_.open(endp.protocol(), ec);
    if (ec)
        co_return ec;

    co_await socket_.async_connect(endp, net_awaitable[ec]);
    if (ec)
        co_return ec;

    rfbProtocolVersionMsg pv = {0};
    boost::asio::async_read(socket_,
                            boost::asio::buffer(&pv, sizeof(pv)),
                            boost::asio::transfer_exactly(12),
                            net_awaitable[ec]);
    if (ec)
        co_return ec;

    int major, minor;
    /* UltraVNC repeater always report version 000.000 to identify itself */
    if (sscanf(pv, rfbProtocolVersionFormat, &major, &minor) != 2) {
        spdlog::warn("Not a valid VNC server ({})", pv);
        co_return boost::system::errc::make_error_code(boost::system::errc::wrong_protocol_type);
    }
    spdlog::info("Connected to VNC repeater, using protocol version {}.{}", major, minor);

}

void client_impl::SetClient2Server(int messageType)
{

}

} // namespace libvnc