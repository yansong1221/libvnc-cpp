#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>

namespace libvnc::encoding {

class rre : public frame_codec
{
public:
    void init() override { }
    std::string codec_name() const override { return "rre"; }

    proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingRRE; }

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

        std::vector<uint8_t> pix(buffer.bytes_per_pixel(), 0);
        boost::endian::big_uint32_buf_t nSubrects {};
        proto::rfbRectangle subrect {};

        boost::system::error_code ec;

        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&nSubrects, sizeof(nSubrects)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        co_await boost::asio::async_read(socket, boost::asio::buffer(pix), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        buffer.fill_rect(rx, ry, rw, rh, pix.data());


        for (std::size_t i = 0; i < nSubrects.value(); i++) {
            co_await boost::asio::async_read(socket, boost::asio::buffer(pix), net_awaitable[ec]);
            if (ec)
                co_return error::make_error(ec);

            co_await boost::asio::async_read(
                socket, boost::asio::buffer(&subrect, sizeof(subrect)), net_awaitable[ec]);
            if (ec)
                co_return error::make_error(ec);

            buffer.fill_rect(rx + subrect.x.value(),
                             ry + subrect.y.value(),
                             subrect.w.value(),
                             subrect.h.value(),
                             pix.data());
        }
        co_return error {};
    }
};


class co_rre : public frame_codec
{
    struct u8_rect
    {
        boost::endian::big_uint8_buf_t x;
        boost::endian::big_uint8_buf_t y;
        boost::endian::big_uint8_buf_t w;
        boost::endian::big_uint8_buf_t h;
    };

public:
    void init() override { }
    std::string codec_name() const override { return "corre"; }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingCoRRE;
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

        std::vector<uint8_t> pix(buffer.bytes_per_pixel(), 0);
        boost::endian::big_uint32_buf_t nSubrects {};
        u8_rect sub_rect {};
        boost::system::error_code ec;

        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&nSubrects, sizeof(nSubrects)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        co_await boost::asio::async_read(socket, boost::asio::buffer(pix), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        buffer.fill_rect(rx, ry, rw, rh, pix.data());


        for (std::size_t i = 0; i < nSubrects.value(); i++) {
            co_await boost::asio::async_read(socket, boost::asio::buffer(pix), net_awaitable[ec]);
            if (ec)
                co_return error::make_error(ec);

            co_await boost::asio::async_read(
                socket, boost::asio::buffer(&sub_rect, sizeof(sub_rect)), net_awaitable[ec]);
            if (ec)
                co_return error::make_error(ec);

            buffer.fill_rect(rx + sub_rect.x.value(),
                             ry + sub_rect.y.value(),
                             sub_rect.w.value(),
                             sub_rect.h.value(),
                             pix.data());
        }
        co_return error {};
    }
};
} // namespace libvnc::encoding