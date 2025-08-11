#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <fmt/format.h>
#include <lzo/lzo1x.h>
#include <spdlog/spdlog.h>

namespace libvnc::encoding {

class ultra : public frame_codec
{
public:
    void reset() override { buffer_.consume(buffer_.size()); }
    std::string codec_name() const override { return "ultra"; }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingUltra;
    }

    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& buffer,
                                         std::shared_ptr<frame_op> op) override
    {
        boost::system::error_code ec;
        boost::endian::big_int32_buf_t nBytes {};

        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&nBytes, sizeof(nBytes)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        if (nBytes.value() == 0)
            co_return error {};

        if (nBytes.value() < 0) {
            co_return error::make_error(custom_error::frame_error,
                                        "ultra error: remote sent negative payload size");
        }
        int rx  = rect.x.value();
        int ry  = rect.y.value();
        int rw  = rect.w.value();
        int rh  = rect.h.value();
        int BPP = buffer.pixel_format().bitsPerPixel.value();

        lzo_uint uncompressedBytes = ((rw * rh) * (BPP / 8));
        if (uncompressedBytes == 0) {
            co_return error::make_error(
                custom_error::frame_error,
                fmt::format(
                    "ultra error: rectangle has 0 uncomressed bytes (({}w * {}h) * ({} / 8))",
                    rw,
                    rh,
                    BPP));
        }
        co_await boost::asio::async_read(
            socket, buffer_, boost::asio::transfer_exactly(nBytes.value()), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        if ((uncompressedBytes % 4) != 0) {
            uncompressedBytes += (4 - (uncompressedBytes % 4));
        }
        decompress_buffer_.resize(uncompressedBytes);

        auto inflateResult = lzo1x_decompress_safe((uint8_t*)buffer_.data().data(),
                                                   nBytes.value(),
                                                   decompress_buffer_.data(),
                                                   &uncompressedBytes,
                                                   nullptr);

        /* Note that uncompressedBytes will be 0 on output overrun */
        if ((rw * rh * (BPP / 8)) != uncompressedBytes)
            spdlog::warn("Ultra decompressed unexpected amount of data ({} != {})\n",
                         (rw * rh * (BPP / 8)),
                         uncompressedBytes);

        /* Put the uncompressed contents of the update on the screen. */
        if (inflateResult == LZO_E_OK) {
            buffer.got_bitmap(decompress_buffer_.data(), rx, ry, rw, rh);
            buffer_.consume(nBytes.value());
        }
        else {
            co_return error::make_error(
                custom_error::frame_error,
                fmt::format("ultra decompress returned error: {}", inflateResult));
        }
        co_return error {};
    }

private:
    boost::asio::streambuf buffer_;
    std::vector<uint8_t> decompress_buffer_;
};
} // namespace libvnc::encoding