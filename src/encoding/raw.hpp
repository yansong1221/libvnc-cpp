#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>

namespace libvnc::encoding {

class raw : public frame_codec
{
public:
    void reset() override { buffer_.consume(buffer_.size()); }
    std::string codec_name() const override { return "raw"; }
    proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingRaw; }

    boost::asio::awaitable<bool> decode(boost::asio::ip::tcp::socket& socket,
                                        const proto::rfbRectangle& rect,
                                        const proto::rfbPixelFormat& format,
                                        std::shared_ptr<frame_op> op) override
    {
        int y = rect.y.value();
        int h = rect.h.value();

        auto bytesPerLine = rect.w.value() * format.bitsPerPixel.value() / 8;

        /* RealVNC 4.x-5.x on OSX can induce bytesPerLine==0,
           usually during GPU accel. */
        /* Regardless of cause, do not divide by zero. */
        auto linesToRead = bytesPerLine ? 1 : 0;

        while (linesToRead && h > 0) {
            if (linesToRead > h)
                linesToRead = h;

            auto bytes = co_await boost::asio::async_read(
                socket, buffer_, boost::asio::transfer_exactly(bytesPerLine * linesToRead));

            op->got_bitmap((const uint8_t*)buffer_.data().data(),
                           rect.x.value(),
                           y,
                           rect.w.value(),
                           linesToRead);

            h -= linesToRead;
            y += linesToRead;

            buffer_.consume(bytes);
        }
        co_return true;
    }

private:
    boost::asio::streambuf buffer_;
};
} // namespace libvnc::encoding