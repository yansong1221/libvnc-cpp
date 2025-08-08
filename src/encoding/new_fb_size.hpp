#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>

namespace libvnc::encoding {

class new_fb_size : public codec
{
public:
    void reset() override { }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingNewFBSize;
    }

    boost::asio::awaitable<bool> decode(boost::asio::ip::tcp::socket& socket,
                                        const proto::rfbRectangle& rect,
                                        const proto::rfbPixelFormat& format,
                                        std::shared_ptr<frame_op> op) override
    {
        op->resize_client_buffer(rect.x.value(), rect.y.value());
        co_return true;
    }
};
} // namespace libvnc::encoding