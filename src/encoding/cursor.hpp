#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>

namespace libvnc::encoding {

class cursor : public codec
{
public:
    void reset() override { }
    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& buffer,
                                         std::shared_ptr<frame_op> op) override
    {
        int xhot   = rect.x.value();
        int yhot   = rect.y.value();
        int width  = rect.w.value();
        int height = rect.h.value();

        auto bytesPerRow   = (width + 7) / 8;
        auto bytesMaskData = bytesPerRow * height;

        if (width * height == 0)
            co_return error {};

        rcSource_.init(width, height, buffer.pixel_format());

        if (auto err = co_await read_rc_source(socket, width, height, bytesMaskData, bytesPerRow);
            err)
            co_return err;


        boost::system::error_code ec;
        /* Read and decode mask data. */
        std::vector<uint8_t> buf;
        buf.resize(bytesMaskData);
        co_await boost::asio::async_read(socket, boost::asio::buffer(buf), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

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
        op->got_cursor_shape(xhot, yhot, rcSource_, rcMask_.data());
        co_return error {};
    }

protected:
    virtual boost::asio::awaitable<error> read_rc_source(boost::asio::ip::tcp::socket& socket,
                                                         int width,
                                                         int height,
                                                         std::size_t bytesMaskData,
                                                         std::size_t bytesPerRow) = 0;

protected:
    frame_buffer rcSource_;
    std::vector<uint8_t> rcMask_;
};

class x_cursor : public cursor
{
public:
    std::string codec_name() const override { return "xcursor"; }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingXCursor;
    }

    boost::asio::awaitable<error> read_rc_source(boost::asio::ip::tcp::socket& socket,
                                                 int width,
                                                 int height,
                                                 std::size_t bytesMaskData,
                                                 std::size_t bytesPerRow) override
    {
        boost::system::error_code ec;
        /* Read and convert background and foreground colors. */
        uint32_t colors[2] = {0};
        proto::rfbXCursorColors rgb {};
        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&rgb, sizeof(rgb)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        colors[0] = rgb24_to_pixel(rcSource_.pixel_format(),
                                   rgb.backRed.value(),
                                   rgb.backGreen.value(),
                                   rgb.backBlue.value());
        colors[1] = rgb24_to_pixel(rcSource_.pixel_format(),
                                   rgb.foreRed.value(),
                                   rgb.foreGreen.value(),
                                   rgb.foreBlue.value());


        /* Read 1bpp pixel data into a temporary buffer. */
        std::vector<uint8_t> buf;
        buf.resize(bytesMaskData);
        co_await boost::asio::async_read(socket, boost::asio::buffer(buf), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        /* Convert 1bpp data to byte-wide color indices. */
        uint8_t* ptr = rcSource_.data();
        for (int y = 0; y < height; y++) {
            int x = 0;
            for (; x < width / 8; x++) {
                for (int b = 7; b >= 0; b--) {
                    *ptr = buf[y * bytesPerRow + x] >> b & 1;
                    ptr += rcSource_.bytes_per_pixel();
                }
            }
            for (int b = 7; b > 7 - width % 8; b--) {
                *ptr = buf[y * bytesPerRow + x] >> b & 1;
                ptr += rcSource_.bytes_per_pixel();
            }
        }
        /* Convert indices into the actual pixel values. */
        switch (rcSource_.bytes_per_pixel()) {
            case 1:
                for (int x = 0; x < width * height; x++)
                    rcSource_.data()[x] = (uint8_t)colors[rcSource_.data()[x]];
                break;
            case 2:
                for (int x = 0; x < width * height; x++)
                    ((uint16_t*)rcSource_.data())[x] = (uint16_t)colors[rcSource_.data()[x * 2]];
                break;
            case 4:
                for (int x = 0; x < width * height; x++)
                    ((uint32_t*)rcSource_.data())[x] = colors[rcSource_.data()[x * 4]];
                break;
        }
        co_return error::make_error(ec);
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
    std::string codec_name() const override { return "richcursor"; }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingRichCursor;
    }
    boost::asio::awaitable<error> read_rc_source(boost::asio::ip::tcp::socket& socket,
                                                 int width,
                                                 int height,
                                                 std::size_t bytesMaskData,
                                                 std::size_t bytesPerRow) override
    {
        boost::system::error_code ec;
        co_await boost::asio::async_read(
            socket, boost::asio::buffer(rcSource_.data(), rcSource_.size()), net_awaitable[ec]);
        co_return error::make_error(ec);
    }
};
} // namespace libvnc::encoding