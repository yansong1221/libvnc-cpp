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
    void reset() override { }
    std::string codec_name() const override { return "ultra"; }
    bool requestLastRectEncoding() const override { return true; }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingUltra;
    }

    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& buffer,
                                         std::shared_ptr<frame_op> op) override
    {
        if (auto err = co_await frame_codec::decode(socket, rect, buffer, op); err)
            co_return err;

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
        int rx         = rect.x.value();
        int ry         = rect.y.value();
        int rw         = rect.w.value();
        int rh         = rect.h.value();
        int byte_pixel = buffer.bytes_per_pixel();

        lzo_uint uncompressedBytes = ((rw * rh) * (byte_pixel));
        if (uncompressedBytes == 0) {
            co_return error::make_error(
                custom_error::frame_error,
                fmt::format("ultra error: rectangle has 0 uncomressed bytes (({}w * {}h) * ({}))",
                            rw,
                            rh,
                            byte_pixel));
        }
        buffer_.resize(nBytes.value());
        co_await boost::asio::async_read(socket, boost::asio::buffer(buffer_), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        if ((uncompressedBytes % 4) != 0) {
            uncompressedBytes += (4 - (uncompressedBytes % 4));
        }
        decompress_buffer_.resize(uncompressedBytes);

        auto inflateResult = lzo1x_decompress_safe(
            buffer_.data(), nBytes.value(), decompress_buffer_.data(), &uncompressedBytes, nullptr);

        /* Note that uncompressedBytes will be 0 on output overrun */
        if ((rw * rh * byte_pixel) != uncompressedBytes)
            spdlog::warn("Ultra decompressed unexpected amount of data ({} != {})\n",
                         (rw * rh * byte_pixel),
                         uncompressedBytes);

        /* Put the uncompressed contents of the update on the screen. */
        if (inflateResult == LZO_E_OK) {
            buffer.got_bitmap(decompress_buffer_.data(), rx, ry, rw, rh);
        }
        else {
            co_return error::make_error(
                custom_error::frame_error,
                fmt::format("ultra decompress returned error: {}", inflateResult));
        }
        co_return error {};
    }

private:
    std::vector<uint8_t> buffer_;
    std::vector<uint8_t> decompress_buffer_;
};
} // namespace libvnc::encoding