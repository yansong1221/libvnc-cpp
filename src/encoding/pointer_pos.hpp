#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>

namespace libvnc::encoding {

class pointer_pos : public codec
{
public:
    void reset() override { }
    std::string codec_name() const override { return "pointer-pos"; }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingPointerPos;
    }

    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& buffer,
                                         std::shared_ptr<frame_op> op) override
    {
        op->handle_cursor_pos(rect.x.value(), rect.y.value());
        co_return error {};
    }
};
} // namespace libvnc::encoding