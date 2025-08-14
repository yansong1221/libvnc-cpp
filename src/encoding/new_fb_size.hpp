#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>

namespace libvnc::encoding {

class new_fb_size : public codec
{
public:
    void init() override { }
    std::string codec_name() const override { return "new-fb-size"; }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingNewFBSize;
    }

    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& buffer,
                                         std::shared_ptr<frame_op> op) override
    {
        op->handle_resize_client_buffer(rect.x.value(), rect.y.value());
        co_return error {};
    }
};
} // namespace libvnc::encoding