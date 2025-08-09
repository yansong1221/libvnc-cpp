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

    boost::asio::awaitable<bool> decode(boost::asio::ip::tcp::socket& socket,
                                        const proto::rfbRectangle& rect,
                                        const proto::rfbPixelFormat& format,
                                        std::shared_ptr<frame_op> op) override
    {
        switch (format.bitsPerPixel.value()) {
            case 8: {
                co_await handle_rre<uint8_t>(
                    op, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            case 16: {
                co_await handle_rre<uint16_t>(
                    op, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            case 32: {
                co_await handle_rre<uint32_t>(
                    op, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            default: break;
        }
        co_return true;
    }

private:
    template<typename PixType>
    boost::asio::awaitable<void> handle_rre(std::shared_ptr<frame_op> op,
                                            boost::asio::ip::tcp::socket& socket,
                                            int rx,
                                            int ry,
                                            int rw,
                                            int rh)
    {
        PixType pix {};
        boost::endian::big_uint32_buf_t nSubrects {};
        proto::rfbRectangle subrect {};

        co_await boost::asio::async_read(socket,
                                         boost::asio::buffer(&nSubrects, sizeof(nSubrects)));
        co_await boost::asio::async_read(socket, boost::asio::buffer(&pix, sizeof(pix)));

        op->got_fill_rect(rx, ry, rw, rh, pix);

        for (int i = 0; i < nSubrects.value(); i++) {
            co_await boost::asio::async_read(socket, boost::asio::buffer(&pix, sizeof(pix)));
            co_await boost::asio::async_read(socket,
                                             boost::asio::buffer(&subrect, sizeof(subrect)));

            op->got_fill_rect(rx + subrect.x.value(),
                              ry + subrect.y.value(),
                              subrect.w.value(),
                              subrect.h.value(),
                              pix);
        }
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

    boost::asio::awaitable<bool> decode(boost::asio::ip::tcp::socket& socket,
                                        const proto::rfbRectangle& rect,
                                        const proto::rfbPixelFormat& format,
                                        std::shared_ptr<frame_op> op) override
    {
        switch (format.bitsPerPixel.value()) {
            case 8: {
                co_await handle_co_rre<uint8_t>(
                    op, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            case 16: {
                co_await handle_co_rre<uint16_t>(
                    op, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            case 32: {
                co_await handle_co_rre<uint32_t>(
                    op, socket, rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
            } break;
            default: break;
        }
        co_return true;
    }

private:
    template<typename PixType>
    boost::asio::awaitable<void> handle_co_rre(std::shared_ptr<frame_op> op,
                                               boost::asio::ip::tcp::socket& socket,
                                               int rx,
                                               int ry,
                                               int rw,
                                               int rh)
    {
        PixType pix {};
        boost::endian::big_uint32_buf_t nSubrects {};
        proto::rfbRectangle subrect {};

        co_await boost::asio::async_read(socket,
                                         boost::asio::buffer(&nSubrects, sizeof(nSubrects)));
        co_await boost::asio::async_read(socket, boost::asio::buffer(&pix, sizeof(pix)));

        op->got_fill_rect(rx, ry, rw, rh, pix);

        auto bytes = co_await boost::asio::async_read(
            socket,
            buffer_,
            boost::asio::transfer_exactly(nSubrects.value() * (4 + (sizeof(PixType)))));

        auto ptr = static_cast<const uint8_t*>(buffer_.data().data());

        for (int i = 0; i < nSubrects.value(); i++) {
            pix = *(PixType*)ptr;
            ptr += sizeof(PixType);
            auto x = *ptr++;
            auto y = *ptr++;
            auto w = *ptr++;
            auto h = *ptr++;

            op->got_fill_rect(rx + x, ry + y, w, h, pix);
        }
        buffer_.consume(bytes);
    }

private:
    boost::asio::streambuf buffer_;
};
} // namespace libvnc::encoding