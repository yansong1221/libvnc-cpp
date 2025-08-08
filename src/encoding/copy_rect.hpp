#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>

namespace libvnc::encoding {

class copy_rect : public frame_codec
{
public:
    std::string codec_name() const override { return "copyrect"; }

    void reset() override { }
    proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingCopyRect; }

    boost::asio::awaitable<bool> decode(boost::asio::ip::tcp::socket& socket,
                                        const proto::rfbRectangle& rect,
                                        const proto::rfbPixelFormat& format,
                                        std::shared_ptr<frame_op> op) override
    {
        proto::rfbCopyRect cr;
        co_await boost::asio::async_read(socket, boost::asio::buffer(&cr, sizeof(cr)));

        op->soft_cursor_lock_area(cr.srcX.value(), cr.srcY.value(), rect.w.value(), rect.h.value());

        op->got_copy_rect(cr.srcX.value(),
                          cr.srcY.value(),
                          rect.w.value(),
                          rect.h.value(),
                          rect.x.value(),
                          rect.y.value());
        co_return true;
    }
};
} // namespace libvnc::encoding