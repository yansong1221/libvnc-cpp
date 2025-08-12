#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <fmt/format.h>
#include <lzo/lzo1x.h>
#include <spdlog/spdlog.h>

namespace libvnc::encoding {

class hextile : public frame_codec
{
    constexpr static uint8_t rfbHextileRaw                 = (1 << 0);
    constexpr static uint8_t rfbHextileBackgroundSpecified = (1 << 1);
    constexpr static uint8_t rfbHextileForegroundSpecified = (1 << 2);
    constexpr static uint8_t rfbHextileAnySubrects         = (1 << 3);
    constexpr static uint8_t rfbHextileSubrectsColoured    = (1 << 4);

public:
    void reset() override { buffer_.consume(buffer_.size()); }
    std::string codec_name() const override { return "hextile"; }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingHextile;
    }

    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& buffer,
                                         std::shared_ptr<frame_op> op) override
    {
        if (auto err = co_await frame_codec::decode(socket, rect, buffer, op); err)
            co_return err;

        int rx = rect.x.value();
        int ry = rect.y.value();
        int rw = rect.w.value();
        int rh = rect.h.value();

        for (int y = ry; y < ry + rh; y += 16) {
            for (int x = rx; x < rx + rw; x += 16) {
                int w = 16, h = 16;
                if (rx + rw - x < 16)
                    w = rx + rw - x;
                if (ry + rh - y < 16)
                    h = ry + rh - y;

                if (auto err = co_await handle_hextile(socket, buffer, op, x, y, w, h); err)
                    co_return err;
            }
        }
        co_return error {};
    }

private:
    boost::asio::awaitable<error> handle_hextile(boost::asio::ip::tcp::socket& socket,
                                                 frame_buffer& frame,
                                                 std::shared_ptr<frame_op> op,
                                                 int x,
                                                 int y,
                                                 int w,
                                                 int h)
    {
        boost::system::error_code ec;
        uint8_t subencoding {};
        uint8_t nSubrects {};

        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&subencoding, sizeof(subencoding)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        if (subencoding & rfbHextileRaw) {
            auto bytesPerLine = w * frame.bytes_per_pixel();

            for (int i = 0; i < h; ++i) {
                auto ptr = frame.data(x, y + i);

                co_await boost::asio::async_read(
                    socket, boost::asio::buffer(ptr, bytesPerLine), net_awaitable[ec]);
                if (ec)
                    co_return error::make_error(ec);
            }
            co_return error {};
        }

        if (subencoding & rfbHextileBackgroundSpecified) {
            std::vector<uint8_t> bg(frame.bytes_per_pixel(), 0);

            co_await boost::asio::async_read(socket, boost::asio::buffer(bg), net_awaitable[ec]);
            if (ec)
                co_return error::make_error(ec);

        }

        co_return error {};
    }


private:
    boost::asio::streambuf buffer_;
    std::vector<uint8_t> decompress_buffer_;
};
} // namespace libvnc::encoding