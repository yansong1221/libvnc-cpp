#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>

namespace libvnc::encoding {

class pointer_pos : public codec
{
public:
    void reset() override { }
    proto::rfbEncoding encoding() const override
    {
        return proto::rfbEncoding::rfbEncodingPointerPos;
    }

    boost::asio::awaitable<bool> decode(boost::asio::ip::tcp::socket& socket,
                                        const proto::rfbRectangle& rect,
                                        const proto::rfbPixelFormat& format,
                                        std::shared_ptr<frame_op> op) override
    {
        op->handle_cursor_pos(rect.x.value(), rect.y.value());
        co_return true;
    }
};
} // namespace libvnc::encoding