#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <fmt/format.h>
#include <lzo/lzo1x.h>
#include <spdlog/spdlog.h>

namespace libvnc::encoding {

class hextile : public frame_codec {
	/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     * Hextile Encoding. The rectangle is divided up into "tiles" of 16x16 pixels,
     * starting at the top left going in left-to-right, top-to-bottom order. If
     * the width of the rectangle is not an exact multiple of 16 then the width of
     * the last tile in each row will be correspondingly smaller. Similarly if the
     * height is not an exact multiple of 16 then the height of each tile in the
     * final row will also be smaller. Each tile begins with a "subencoding" type
     * byte, which is a mask made up of a number of bits. If the Raw bit is set
     * then the other bits are irrelevant; w*h pixel values follow (where w and h
     * are the width and height of the tile). Otherwise the tile is encoded in a
     * similar way to RRE, except that the position and size of each subrectangle
     * can be specified in just two bytes. The other bits in the mask are as
     * follows:
     *
     * BackgroundSpecified - if set, a pixel value follows which specifies
     *    the background colour for this tile. The first non-raw tile in a
     *    rectangle must have this bit set. If this bit isn't set then the
     *    background is the same as the last tile.
     *
     * ForegroundSpecified - if set, a pixel value follows which specifies
     *    the foreground colour to be used for all subrectangles in this tile.
     *    If this bit is set then the SubrectsColoured bit must be zero.
     *
     * AnySubrects - if set, a single byte follows giving the number of
     *    subrectangles following. If not set, there are no subrectangles (i.e.
     *    the whole tile is just solid background colour).
     *
     * SubrectsColoured - if set then each subrectangle is preceded by a pixel
     *    value giving the colour of that subrectangle. If not set, all
     *    subrectangles are the same colour, the foreground colour;  if the
     *    ForegroundSpecified bit wasn't set then the foreground is the same as
     *    the last tile.
     *
     * The position and size of each subrectangle is specified in two bytes. The
     * Pack macros below can be used to generate the two bytes from x, y, w, h,
     * and the Extract macros can be used to extract the x, y, w, h values from
     * the two bytes.
     */
	constexpr static uint8_t rfbHextileRaw = (1 << 0);
	constexpr static uint8_t rfbHextileBackgroundSpecified = (1 << 1);
	constexpr static uint8_t rfbHextileForegroundSpecified = (1 << 2);
	constexpr static uint8_t rfbHextileAnySubrects = (1 << 3);
	constexpr static uint8_t rfbHextileSubrectsColoured = (1 << 4);

public:
	void init() override {}
	std::string codec_name() const override { return "hextile"; }
	proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingHextile; }

	boost::asio::awaitable<error> decode(socket_stream &socket, const proto::rfbRectangle &rect,
					     frame_buffer &buffer, std::shared_ptr<client_op> op) override
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
		co_return error{};
	}

private:
	boost::asio::awaitable<error> handle_hextile(socket_stream &socket, frame_buffer &frame,
						     std::shared_ptr<client_op> op, int x, int y, int w, int h)
	{
		boost::system::error_code ec;
		uint8_t subencoding{};
		uint8_t nSubrects{};
		std::vector<uint8_t> bg(frame.bytes_per_pixel(), 0);
		std::vector<uint8_t> fg(frame.bytes_per_pixel(), 0);

		co_await boost::asio::async_read(socket, boost::asio::buffer(&subencoding, sizeof(subencoding)),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		if (subencoding & rfbHextileRaw) {
			auto bytesPerLine = w * frame.bytes_per_pixel();

			for (int i = 0; i < h; ++i) {
				auto ptr = frame.data(x, y + i);

				co_await boost::asio::async_read(socket, boost::asio::buffer(ptr, bytesPerLine),
								 net_awaitable[ec]);
				if (ec)
					co_return error::make_error(ec);
			}
			co_return error{};
		}

		if (subencoding & rfbHextileBackgroundSpecified) {

			co_await boost::asio::async_read(socket, boost::asio::buffer(bg), net_awaitable[ec]);
			if (ec)
				co_return error::make_error(ec);
		}
		frame.fill_rect(x, y, w, h, bg.data());

		if (subencoding & rfbHextileForegroundSpecified) {
			co_await boost::asio::async_read(socket, boost::asio::buffer(fg), net_awaitable[ec]);
			if (ec)
				co_return error::make_error(ec);
		}
		if (!(subencoding & rfbHextileAnySubrects)) {
			co_return error{};
		}

		co_await boost::asio::async_read(socket, boost::asio::buffer(&nSubrects, sizeof(nSubrects)),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		if (subencoding & rfbHextileSubrectsColoured) {
			for (int i = 0; i < nSubrects; i++) {
				co_await boost::asio::async_read(socket, boost::asio::buffer(fg), net_awaitable[ec]);
				if (ec)
					co_return error::make_error(ec);

				boost::endian::big_uint16_buf_t sub_rect{};
				co_await boost::asio::async_read(
					socket, boost::asio::buffer(&sub_rect, sizeof(sub_rect)), net_awaitable[ec]);
				if (ec)
					co_return error::make_error(ec);

				auto sx = sub_rect.value() >> 12;
				auto sy = (sub_rect.value() >> 8) & 0xF;
				auto sw = ((sub_rect.value() >> 4) & 0xF) + 1;
				auto sh = (sub_rect.value() & 0xF) + 1;
				frame.fill_rect(x + sx, y + sy, sw, sh, fg.data());
			}
		} else {
			for (int i = 0; i < nSubrects; i++) {
				boost::endian::big_uint16_buf_t sub_rect{};
				co_await boost::asio::async_read(
					socket, boost::asio::buffer(&sub_rect, sizeof(sub_rect)), net_awaitable[ec]);
				if (ec)
					co_return error::make_error(ec);

				auto sx = sub_rect.value() >> 12;
				auto sy = (sub_rect.value() >> 8) & 0xF;
				auto sw = ((sub_rect.value() >> 4) & 0xF) + 1;
				auto sh = (sub_rect.value() & 0xF) + 1;
				frame.fill_rect(x + sx, y + sy, sw, sh, fg.data());
			}
		}

		co_return error{};
	}
};
} // namespace libvnc::encoding