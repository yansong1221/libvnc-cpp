#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <fmt/format.h>
#include <lzo/lzo1x.h>
#include <spdlog/spdlog.h>

namespace libvnc::encoding {

class ultra : public frame_codec {
public:
	void init() override {}
	std::string codec_name() const override { return "ultra"; }
	bool request_last_rect_encoding() const override { return true; }
	proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingUltra; }

	boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket &socket, const proto::rfbRectangle &rect,
					     frame_buffer &buffer, std::shared_ptr<frame_op> op) override
	{
		if (auto err = co_await frame_codec::decode(socket, rect, buffer, op); err)
			co_return err;

		boost::system::error_code ec;
		boost::endian::big_int32_buf_t nBytes{};

		co_await boost::asio::async_read(socket, boost::asio::buffer(&nBytes, sizeof(nBytes)),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		if (nBytes.value() == 0)
			co_return error{};

		if (nBytes.value() < 0) {
			co_return error::make_error(custom_error::frame_error,
						    "ultra error: remote sent negative payload size");
		}
		int rx = rect.x.value();
		int ry = rect.y.value();
		int rw = rect.w.value();
		int rh = rect.h.value();
		int byte_pixel = buffer.bytes_per_pixel();

		lzo_uint uncompressedBytes = ((rw * rh) * (byte_pixel));
		if (uncompressedBytes == 0) {
			co_return error::make_error(
				custom_error::frame_error,
				fmt::format("ultra error: rectangle has 0 uncomressed bytes (({}w * {}h) * ({}))", rw,
					    rh, byte_pixel));
		}
		buffer_.resize(nBytes.value());
		co_await boost::asio::async_read(socket, boost::asio::buffer(buffer_), net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		if ((uncompressedBytes % 4) != 0) {
			uncompressedBytes += (4 - (uncompressedBytes % 4));
		}
		decompress_buffer_.resize(uncompressedBytes);

		auto inflateResult = lzo1x_decompress_safe(buffer_.data(), nBytes.value(), decompress_buffer_.data(),
							   &uncompressedBytes, nullptr);

		/* Note that uncompressedBytes will be 0 on output overrun */
		if ((rw * rh * byte_pixel) != uncompressedBytes)
			spdlog::warn("Ultra decompressed unexpected amount of data ({} != {})\n",
				     (rw * rh * byte_pixel), uncompressedBytes);

		/* Put the uncompressed contents of the update on the screen. */
		if (inflateResult == LZO_E_OK) {
			buffer.got_bitmap(decompress_buffer_.data(), rx, ry, rw, rh);
		} else {
			co_return error::make_error(custom_error::frame_error,
						    fmt::format("ultra decompress returned error: {}", inflateResult));
		}
		co_return error{};
	}

private:
	std::vector<uint8_t> buffer_;
	std::vector<uint8_t> decompress_buffer_;
};

class ultra_zip : public frame_codec {
public:
	void init() override {}
	std::string codec_name() const override { return "ultrazip"; }
	bool request_last_rect_encoding() const override { return true; }
	proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingUltraZip; }

	boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket &socket, const proto::rfbRectangle &rect,
					     frame_buffer &frame, std::shared_ptr<frame_op> op) override
	{
		if (auto err = co_await frame_codec::decode(socket, rect, frame, op); err)
			co_return err;

		boost::system::error_code ec;
		boost::endian::big_int32_buf_t nBytes{};

		co_await boost::asio::async_read(socket, boost::asio::buffer(&nBytes, sizeof(nBytes)),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		if (nBytes.value() == 0)
			co_return error{};

		if (nBytes.value() < 0) {
			co_return error::make_error(custom_error::frame_error,
						    "ultrazip error: remote sent negative payload size");
		}
		int rx = rect.x.value();
		int ry = rect.y.value();
		int rw = rect.w.value();
		int rh = rect.h.value();
		int byte_pixel = frame.bytes_per_pixel();

		lzo_uint uncompressedBytes = ry + (rw * 65535);
		unsigned int numCacheRects = rx;

		if (uncompressedBytes == 0) {
			co_return error::make_error(
				custom_error::frame_error,
				fmt::format(
					R"(ultrazip error: rectangle has 0 uncomressed bytes ({}y + ({}w * 65535)) ({} rectangles))",
					ry, rw, rx));
		}
		buffer_.resize(nBytes.value());
		co_await boost::asio::async_read(socket, boost::asio::buffer(buffer_), net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		uncompressedBytes += 500;
		if ((uncompressedBytes % 4) != 0) {
			uncompressedBytes += (4 - (uncompressedBytes % 4));
		}
		decompress_buffer_.resize(uncompressedBytes);

		auto inflateResult = lzo1x_decompress_safe(buffer_.data(), nBytes.value(), decompress_buffer_.data(),
							   &uncompressedBytes, nullptr);

		if (inflateResult != LZO_E_OK) {
			co_return error::make_error(custom_error::frame_error,
						    fmt::format("ultra decompress returned error: {}", inflateResult));
		}

		/* Put the uncompressed contents of the update on the screen. */
		auto ptr = decompress_buffer_.data();
		for (std::size_t i = 0; i < numCacheRects; i++) {
			boost::endian::big_uint16_buf_t sx, sy, sw, sh;
			boost::endian::big_uint32_buf_t se;

			memcpy((char *)&sx, ptr, 2);
			ptr += 2;
			memcpy((char *)&sy, ptr, 2);
			ptr += 2;
			memcpy((char *)&sw, ptr, 2);
			ptr += 2;
			memcpy((char *)&sh, ptr, 2);
			ptr += 2;
			memcpy((char *)&se, ptr, 4);
			ptr += 4;

			if (se.value() == proto::rfbEncodingRaw) {
				frame.got_bitmap(ptr, sx.value(), sy.value(), sw.value(), sh.value());
				ptr += ((sw.value() * sh.value()) * byte_pixel);
			}
		}
		co_return error{};
	}

private:
	std::vector<uint8_t> buffer_;
	std::vector<uint8_t> decompress_buffer_;
};
} // namespace libvnc::encoding