#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>

namespace libvnc::encoding {

class supported_encodings : public codec
{
public:
    std::string codec_name() const override { return "supported-encodings"; }

    void reset() override { }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingSupportedEncodings;
    }

    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& frame,
                                         std::shared_ptr<frame_op> op) override
    { /* rect.r.w=byte count, rect.r.h=# of encodings */
        boost::system::error_code ec;
        std::vector<uint8_t> buffer;
        buffer.resize(rect.w.value());

        co_await boost::asio::async_read(socket, boost::asio::buffer(buffer), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        /* buffer now contains rect.r.h # of uint32_t encodings that the server supports
         */
        /* currently ignored by this library */
        co_return error {};
    }
};
} // namespace libvnc::encoding