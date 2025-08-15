#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <zstr.hpp>

namespace libvnc::encoding {

class tight : public frame_codec
{
public:
    void init() override
    {
        buffer_.consume(buffer_.size());
        z_streams_.fill(std::move(
            std::make_unique<zstr::istream>(&buffer_, zstr::default_buff_size, false, 15)));
    }
    std::string codec_name() const override { return "tight"; }
    proto::rfbEncoding encoding_code() const override { return proto::rfbEncodingTight; }
    bool request_compress_level() const override { return true; }
    bool request_quality_level() const override { return true; }

    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& frame,
                                         std::shared_ptr<frame_op> op) override
    {
        if (auto err = co_await frame_codec::decode(socket, rect, frame, op); err)
            co_return err;

        int x = rect.x.value();
        int y = rect.y.value();
        int w = rect.w.value();
        int h = rect.h.value();

        boost::system::error_code ec;
        boost::endian::big_uint32_buf_t nBytes {};

        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&nBytes, sizeof(nBytes)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        auto total_size = nBytes.value();

        co_await boost::asio::async_read(
            socket, buffer_, boost::asio::transfer_exactly(total_size), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        auto row_bytes = w * frame.bytes_per_pixel();

        if (buffer_.size() != 0)
            co_return error::make_error(custom_error::frame_error, "zlib error");

        co_return error {};
    }

private:
    boost::asio::streambuf buffer_;
    std::array<std::unique_ptr<zstr::istream>, 4> z_streams_;
};
} // namespace libvnc::encoding