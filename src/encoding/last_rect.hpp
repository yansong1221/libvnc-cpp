#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>

namespace libvnc::encoding {

class last_rect : public codec
{
public:
    void reset() override { }
    proto::rfbEncoding encoding() const override { return proto::rfbEncoding::rfbEncodingLastRect; }

    boost::asio::awaitable<bool> decode(boost::asio::ip::tcp::socket& socket,
                                        const proto::rfbRectangle& rect,
                                        const proto::rfbPixelFormat& format,
                                        std::shared_ptr<frame_op> op) override
    {
        co_return true;
    }
};
} // namespace libvnc::encoding