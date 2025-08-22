#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <zstr.hpp>
#include "helper.hpp"

namespace libvnc::encoding {

namespace detail {
static inline uint32_t readOpaque24A(std::istream &is)
{
	uint32_t r = 0;
	is.read(&((char *)&r)[0], 1);
	is.read(&((char *)&r)[1], 1);
	is.read(&((char *)&r)[2], 1);
	return r;
}
static inline uint32_t readOpaque24B(std::istream &is)
{
	uint32_t r = 0;
	is.read(&((char *)&r)[1], 1);
	is.read(&((char *)&r)[2], 1);
	is.read(&((char *)&r)[3], 1);
	return r;
}

static inline uint8_t read_u8(std::istream &is)
{
	uint8_t r = 0;
	is.read((char *)&r, 1);
	return r;
}

inline uint8_t readOpaque8(std::istream &is)
{
	return read_u8(is);
}
inline uint16_t readOpaque16(std::istream &is)
{
	uint16_t r;
	((uint8_t *)&r)[0] = read_u8(is);
	((uint8_t *)&r)[1] = read_u8(is);
	return r;
}
inline uint32_t readOpaque32(std::istream &is)
{
	uint32_t r;
	((uint8_t *)&r)[0] = read_u8(is);
	((uint8_t *)&r)[1] = read_u8(is);
	((uint8_t *)&r)[2] = read_u8(is);
	((uint8_t *)&r)[3] = read_u8(is);
	return r;
}

template<class T> static inline T readPixel(std::istream &is)
{
	if (sizeof(T) == 1)
		return readOpaque8(is);
	if (sizeof(T) == 2)
		return readOpaque16(is);
	if (sizeof(T) == 4)
		return readOpaque32(is);
}

} // namespace detail

class zrle : public frame_codec {

	constexpr static auto rfbZRLETileWidth = 64;
	constexpr static auto rfbZRLETileHeight = 64;

	class bits_reader {
	public:
		bits_reader(std::istream &is) : is_(is) {}

		uint32_t read(int bpp)
		{
			uint32_t result = 0;
			if (bpp % 8 == 0) {
				for (int i = 0; i < bpp / 8; ++i) {
					result = (result << 8) | read_byte();
				}
			} else {
				for (int i = 0; i < bpp; ++i) {
					result = (result << 1) | read_bit();
				}
			}
			return result;
		}

		int read_bit()
		{
			if (bit_pos_ == 8) {
				cur_byte_ = read_byte();
				bit_pos_ = 0;
			}
			int bit = (cur_byte_ >> (7 - bit_pos_)) & 1;
			++bit_pos_;
			return bit;
		}

		int read_byte()
		{
			uint8_t byte = 0;
			is_.read(reinterpret_cast<char *>(&byte), sizeof(byte));
			return byte;
		}

	private:
		std::istream &is_;
		int bit_pos_ = 8;
		uint8_t cur_byte_ = 0;
	};

public:
	void init() override
	{
		read_buffer_.consume(read_buffer_.size());
		z_is_ = std::make_unique<zstr::istream>(&read_buffer_, zstr::default_buff_size, false, 15);
	}
	std::string codec_name() const override { return "zrle"; }
	proto::rfbEncoding encoding_code() const override { return proto::rfbEncodingZRLE; }

	boost::asio::awaitable<error> decode(socket_stream &socket, const proto::rfbRectangle &rect,
					     frame_buffer &frame, std::shared_ptr<client_op> op) noexcept override
	{
		if (auto err = co_await frame_codec::decode(socket, rect, frame, op); err)
			co_return err;

		boost::system::error_code ec;
		boost::endian::big_uint32_buf_t nBytes{};

		co_await boost::asio::async_read(socket, boost::asio::buffer(&nBytes, sizeof(nBytes)),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		int remaining = nBytes.value();

		int rx = rect.x.value();
		int ry = rect.y.value();
		int rw = rect.w.value();
		int rh = rect.h.value();

		auto format = frame.pixel_format();

		uint32_t maxColor = format.max_color();
		uint8_t bytes_per_pixel = format.bytes_per_pixel();

		bool fitsInLS3Bytes = maxColor < (1 << 24);
		bool fitsInMS3Bytes = (maxColor & 0xff) == 0;
		bool isLowCPixel =
			(bytes_per_pixel == 4) && (format.depth.value() <= 24) &&
			((fitsInLS3Bytes && !format.bigEndian.value()) || (fitsInMS3Bytes && format.bigEndian.value()));
		bool isHighCPixel =
			(bytes_per_pixel == 4) && (format.depth.value() <= 24) &&
			((fitsInLS3Bytes && format.bigEndian.value()) || (fitsInMS3Bytes && !format.bigEndian.value()));

		co_await boost::asio::async_read(socket, read_buffer_, boost::asio::transfer_exactly(remaining),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		for (int j = 0; j < rh; j += rfbZRLETileHeight) {
			for (int i = 0; i < rw; i += rfbZRLETileWidth) {
				int subWidth = (i + rfbZRLETileWidth > rw) ? rw - i : rfbZRLETileWidth;
				int subHeight = (j + rfbZRLETileHeight > rh) ? rh - j : rfbZRLETileHeight;

				if (bytes_per_pixel == 1) {
					handle_zrle_tile<uint8_t>(frame, *z_is_, isLowCPixel, isHighCPixel, rx + i,
								  ry + j, subWidth, subHeight);
				} else if (bytes_per_pixel == 2) {
					handle_zrle_tile<uint16_t>(frame, *z_is_, isLowCPixel, isHighCPixel, rx + i,
								   ry + j, subWidth, subHeight);
				} else if (bytes_per_pixel == 4) {
					handle_zrle_tile<uint32_t>(frame, *z_is_, isLowCPixel, isHighCPixel, rx + i,
								   ry + j, subWidth, subHeight);
				}
			}
		}

		if (read_buffer_.size() != 0) {
			co_return error::make_error(custom_error::frame_error, "zrle zlib error");
		}
		co_return error{};
	}
	template<typename T>
	void handle_zrle_tile(frame_buffer &frame, std::istream &is, bool isLowCPixel, bool isHighCPixel, int rx,
			      int ry, int rw, int rh) noexcept
	{
		auto format = frame.pixel_format();
		auto bbp = format.bitsPerPixel.value();

		int mode = detail::read_u8(is);
		bool rle = mode & 128;
		int palSize = mode & 127;
		T palette[128] = {0};

		for (int i = 0; i < palSize; i++) {
			if (isLowCPixel)
				palette[i] = detail::readOpaque24A(is);
			else if (isHighCPixel)
				palette[i] = detail::readOpaque24B(is);
			else
				palette[i] = detail::readPixel<T>(is);
		}
		if (palSize == 1) {
			T pix = palette[0];
			frame.fill_rect(rx, ry, rw, rh, (uint8_t *)&pix);
			return;
		}
		if (!rle) {
			if (palSize == 0) {
				if (isLowCPixel || isHighCPixel) {
					for (int y = 0; y < rh; ++y) {
						for (int x = 0; x < rw; ++x) {
							auto ptr = frame.data(rx + x, ry + y);
							if (isLowCPixel)
								*((T *)ptr) = detail::readOpaque24A(is);
							else
								*((T *)ptr) = detail::readOpaque24B(is);
						}
					}
				} else {
					auto row_bytes = rw * sizeof(T);
					for (int y = 0; y < rh; ++y) {
						auto ptr = frame.data(rx, ry + y);
						is.read((char *)ptr, row_bytes);
					}
				}
			} else {
				// packed pixels
				int bppp = ((palSize > 16) ? 8 : ((palSize > 4) ? 4 : ((palSize > 2) ? 2 : 1)));

				for (int y = 0; y < rh; ++y) {

					bits_reader reader(is);

					for (int x = 0; x < rw; ++x) {
						int index = reader.read(bppp);
						frame.got_bitmap((uint8_t *)&palette[index], rx + x, ry + y, 1, 1);
					}
				}
			}
		} else {
			if (palSize == 0) {
				int i = 0, j = 0;
				while (j < rh) {
					// plain RLE
					T pix = 0;
					if (isLowCPixel)
						pix = detail::readOpaque24A(is);
					else if (isHighCPixel)
						pix = detail::readOpaque24B(is);
					else
						pix = detail::readPixel<T>(is);

					int length = 1;
					uint8_t b = 0;
					do {
						b = detail::read_u8(is);
						length += b;
					} while (b == 255);

					while (j < rh && length > 0) {
						frame.got_bitmap((uint8_t *)&pix, rx + i, ry + j, 1, 1);
						length--;
						i++;
						if (i >= rw) {
							i = 0;
							j++;
						}
					}
				}
			} else {
				// palette RLE

				int i = 0, j = 0;
				while (j < rh) {
					int index = detail::read_u8(is);
					int length = 1;

					if (index & 0x80) {
						uint8_t b = 0;
						do {
							b = detail::read_u8(is);
							length += b;
						} while (b == 0xFF);
					}
					index &= 0x7F;

					while (j < rh && length > 0) {
						frame.got_bitmap((uint8_t *)&palette[index], rx + i, ry + j, 1, 1);
						length--;
						i++;
						if (i >= rw) {
							i = 0;
							j++;
						}
					}
				}
			}
		}
	}

private:
	boost::asio::streambuf read_buffer_;
	std::unique_ptr<zstr::istream> z_is_;
};
} // namespace libvnc::encoding