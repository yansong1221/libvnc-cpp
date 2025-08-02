#pragma once
#include "rfb.h"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace libvnc {
class client_impl
{
public:
    client_impl(const boost::asio::any_io_executor& executor);

public:
    boost::asio::awaitable<boost::system::error_code>
    connect(const boost::asio::ip::tcp::endpoint& endp);

private:
    void SetClient2Server(int messageType);

private:
    boost::asio::ip::tcp::socket socket_;

    typedef struct
    {
        uint8_t client2server[32]; /* maximum of 256 message types (256/8)=32 */
        uint8_t server2client[32]; /* maximum of 256 message types (256/8)=32 */
    } rfbSupportedMessages;
    rfbSupportedMessages supportedMessages_;
};
} // namespace libvnc