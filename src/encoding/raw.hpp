#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>

namespace libvnc::encoding {

class raw : public frame_codec
{
public:
    void init() override { }
    std::string codec_name() const override { return "raw"; }
    proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingRaw; }

    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& buffer,
                                         std::shared_ptr<frame_op> op) override
    {
        if (auto err = co_await frame_codec::decode(socket, rect, buffer, op); err)
            co_return err;

        boost::system::error_code ec;

        int x = rect.x.value();
        int y = rect.y.value();
        int w = rect.w.value();
        int h = rect.h.value();

        auto bytesPerLine = w * buffer.bytes_per_pixel();

        for (int i = 0; i < h; ++i) {
            auto ptr = buffer.data(x, y + i);

            co_await boost::asio::async_read(
                socket, boost::asio::buffer(ptr, bytesPerLine), net_awaitable[ec]);
            if (ec)
                co_return error::make_error(ec);
        }
        co_return error {};
    }
};
} // namespace libvnc::encoding