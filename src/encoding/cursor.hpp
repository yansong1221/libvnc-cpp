#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>

namespace libvnc::encoding {

class cursor : public codec
{
public:
    void reset() override { }
    boost::asio::awaitable<bool> decode(boost::asio::ip::tcp::socket& socket,
                                        const proto::rfbRectangle& rect,
                                        const proto::rfbPixelFormat& format,
                                        std::shared_ptr<frame_op> op) override
    {
        int xhot   = rect.x.value();
        int yhot   = rect.y.value();
        int width  = rect.w.value();
        int height = rect.h.value();

        auto bytesPerPixel = format.bitsPerPixel.value() / 8;
        auto bytesPerRow   = (width + 7) / 8;
        auto bytesMaskData = bytesPerRow * height;

        if (width * height == 0)
            co_return true;

        rcSource_.resize((size_t)width * height * bytesPerPixel);
        // rcMask_.resize(bytesMaskData);

        co_await read_rc_source(
            socket, width, height, bytesMaskData, bytesPerRow, bytesPerPixel, format);

        /* Read and decode mask data. */
        std::vector<uint8_t> buf;
        buf.resize(bytesMaskData);
        co_await boost::asio::async_read(socket, boost::asio::buffer(buf));

        rcMask_.resize((size_t)width * height);

        uint8_t* ptr = rcMask_.data();
        for (int y = 0; y < height; y++) {
            int x = 0;
            for (; x < width / 8; x++) {
                for (int b = 7; b >= 0; b--) {
                    *ptr++ = buf[y * bytesPerRow + x] >> b & 1;
                }
            }
            for (int b = 7; b > 7 - width % 8; b--) {
                *ptr++ = buf[y * bytesPerRow + x] >> b & 1;
            }
        }
        op->got_cursor_shape(xhot, yhot, width, height, bytesPerPixel);
    }

protected:
    virtual boost::asio::awaitable<bool> read_rc_source(boost::asio::ip::tcp::socket& socket,
                                                        int width,
                                                        int height,
                                                        std::size_t bytesMaskData,
                                                        std::size_t bytesPerRow,
                                                        std::size_t bytesPerPixel,
                                                        const proto::rfbPixelFormat& format) = 0;

protected:
    std::vector<uint8_t> rcSource_;
    std::vector<uint8_t> rcMask_;
};

class x_cursor : public cursor
{
public:
    proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingXCursor; }

    boost::asio::awaitable<bool> read_rc_source(boost::asio::ip::tcp::socket& socket,
                                                int width,
                                                int height,
                                                std::size_t bytesMaskData,
                                                std::size_t bytesPerRow,
                                                std::size_t bytesPerPixel,
                                                const proto::rfbPixelFormat& format) override
    {
        /* Read and convert background and foreground colors. */
        uint32_t colors[2] = {0};
        proto::rfbXCursorColors rgb {};
        co_await boost::asio::async_read(socket, boost::asio::buffer(&rgb, sizeof(rgb)));

        colors[0] = rgb24_to_pixel(
            format, rgb.backRed.value(), rgb.backGreen.value(), rgb.backBlue.value());
        colors[1] = rgb24_to_pixel(
            format, rgb.foreRed.value(), rgb.foreGreen.value(), rgb.foreBlue.value());


        /* Read 1bpp pixel data into a temporary buffer. */
        std::vector<uint8_t> buf;
        buf.resize(bytesMaskData);
        co_await boost::asio::async_read(socket, boost::asio::buffer(buf));

        /* Convert 1bpp data to byte-wide color indices. */
        uint8_t* ptr = rcSource_.data();
        for (int y = 0; y < height; y++) {
            int x = 0;
            for (; x < width / 8; x++) {
                for (int b = 7; b >= 0; b--) {
                    *ptr = buf[y * bytesPerRow + x] >> b & 1;
                    ptr += bytesPerPixel;
                }
            }
            for (int b = 7; b > 7 - width % 8; b--) {
                *ptr = buf[y * bytesPerRow + x] >> b & 1;
                ptr += bytesPerPixel;
            }
        }
        /* Convert indices into the actual pixel values. */
        switch (bytesPerPixel) {
            case 1:
                for (int x = 0; x < width * height; x++)
                    rcSource_[x] = (uint8_t)colors[rcSource_[x]];
                break;
            case 2:
                for (int x = 0; x < width * height; x++)
                    ((uint16_t*)rcSource_.data())[x] = (uint16_t)colors[rcSource_[x * 2]];
                break;
            case 4:
                for (int x = 0; x < width * height; x++)
                    ((uint32_t*)rcSource_.data())[x] = colors[rcSource_[x * 4]];
                break;
        }
        co_return true;
    }

private:
    inline static uint32_t
    rgb24_to_pixel(const proto::rfbPixelFormat& format, uint8_t r, uint8_t g, uint8_t b)
    {
        return ((((uint32_t)(r) & 0xFF) * format.redMax.value() + 127) / 255
                    << format.redShift.value() |
                (((uint32_t)(g) & 0xFF) * format.greenMax.value() + 127) / 255
                    << format.greenShift.value() |
                (((uint32_t)(b) & 0xFF) * format.blueMax.value() + 127) / 255
                    << format.blueShift.value());
    }
};

class rich_cursor : public cursor
{
public:
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingRichCursor;
    }
    boost::asio::awaitable<bool> read_rc_source(boost::asio::ip::tcp::socket& socket,
                                                int width,
                                                int height,
                                                std::size_t bytesMaskData,
                                                std::size_t bytesPerRow,
                                                std::size_t bytesPerPixel,
                                                const proto::rfbPixelFormat& format) override
    {
        co_await boost::asio::async_read(socket, boost::asio::buffer(rcSource_));
        co_return true;
    }
};
} // namespace libvnc::encoding