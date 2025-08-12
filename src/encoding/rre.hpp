#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>

namespace libvnc::encoding {

class rre : public frame_codec
{
public:
    void reset() override { }
    std::string codec_name() const override { return "rre"; }

    proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingRRE; }

    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& buffer,
                                         std::shared_ptr<frame_op> op) override
    {
        switch (buffer.pixel_format().bitsPerPixel.value()) {
            case 8: {
                co_return co_await handle_rre<uint8_t>(
                    buffer, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            case 16: {
                co_return co_await handle_rre<uint16_t>(
                    buffer, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            case 32: {
                co_return co_await handle_rre<uint32_t>(
                    buffer, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            default: break;
        }
        co_return error::make_error(
            boost::system::errc::make_error_code(boost::system::errc::wrong_protocol_type));
    }

private:
    template<typename PixType>
    boost::asio::awaitable<error> handle_rre(
        frame_buffer& buffer, boost::asio::ip::tcp::socket& socket, int rx, int ry, int rw, int rh)
    {
        PixType pix {};
        boost::endian::big_uint32_buf_t nSubrects {};
        proto::rfbRectangle subrect {};

        boost::system::error_code ec;

        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&nSubrects, sizeof(nSubrects)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&pix, sizeof(pix)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        buffer.fill_rect(rx, ry, rw, rh, pix);

        for (int i = 0; i < nSubrects.value(); i++) {
            co_await boost::asio::async_read(
                socket, boost::asio::buffer(&pix, sizeof(pix)), net_awaitable[ec]);
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
                             pix);
        }
        co_return error {};
    }
};


class co_rre : public frame_codec
{
public:
    void reset() override { buffer_.consume(buffer_.size()); }
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
        switch (buffer.pixel_format().bitsPerPixel.value()) {
            case 8: {
                co_return co_await handle_co_rre<uint8_t>(
                    buffer, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            case 16: {
                co_return co_await handle_co_rre<uint16_t>(
                    buffer, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            case 32: {
                co_return co_await handle_co_rre<uint32_t>(
                    buffer, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            default: break;
        }
        co_return error::make_error(
            boost::system::errc::make_error_code(boost::system::errc::wrong_protocol_type));
    }

private:
    template<typename PixType>
    boost::asio::awaitable<error> handle_co_rre(
        frame_buffer& buffer, boost::asio::ip::tcp::socket& socket, int rx, int ry, int rw, int rh)
    {
        PixType pix {};
        boost::endian::big_uint32_buf_t nSubrects {};
        proto::rfbRectangle subrect {};
        boost::system::error_code ec;

        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&nSubrects, sizeof(nSubrects)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&pix, sizeof(pix)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        buffer.fill_rect(rx, ry, rw, rh, pix);

        auto bytes = co_await boost::asio::async_read(
            socket,
            buffer_,
            boost::asio::transfer_exactly(nSubrects.value() * (4 + (sizeof(PixType)))),
            net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        std::istream is(&buffer_);
        uint8_t x, y, w, h;
        for (int i = 0; i < nSubrects.value(); i++) {
            is.read((char*) & pix, sizeof(pix));
            is.read((char*)&x, sizeof(x));
            is.read((char*)&y, sizeof(y));
            is.read((char*)&w, sizeof(w));
            is.read((char*)&h, sizeof(h));
            buffer.fill_rect(rx + x, ry + y, w, h, pix);
        }
        co_return error {};
    }

private:
    boost::asio::streambuf buffer_;
};
} // namespace libvnc::encoding