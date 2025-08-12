#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>

namespace libvnc::encoding {

class copy_rect : public frame_codec
{
public:
    std::string codec_name() const override { return "copyrect"; }

    void reset() override { }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingCopyRect;
    }

    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& buffer,
                                         std::shared_ptr<frame_op> op) override
    {
        if (auto err = co_await frame_codec::decode(socket, rect, buffer, op); err)
            co_return err;

        boost::system::error_code ec;
        proto::rfbCopyRect cr {};
        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&cr, sizeof(cr)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        op->soft_cursor_lock_area(cr.srcX.value(), cr.srcY.value(), rect.w.value(), rect.h.value());

        buffer.copy_rect(cr.srcX.value(),
                         cr.srcY.value(),
                         rect.w.value(),
                         rect.h.value(),
                         rect.x.value(),
                         rect.y.value());
        co_return error::make_error(ec);
    }
};
} // namespace libvnc::encoding