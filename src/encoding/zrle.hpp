#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <zstr.hpp>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filter/zstd.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/device/back_inserter.hpp>

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
	zrle() {}
	~zrle()
	{
		if (decompStreamInited) {
			inflateEnd(&decompStream);
			decompStreamInited = false;
		}
	}
	void init() override
	{
		decompress_buffer_.consume(decompress_buffer_.size());
		//z_is_ = std::make_unique<zstr::istream>(&buffer_, zstr::default_buff_size, false, 15);

		if (decompStreamInited) {
			inflateEnd(&decompStream);
			decompStreamInited = false;
		}
	}
	std::string codec_name() const override { return "zrle"; }
	proto::rfbEncoding encoding_code() const override { return proto::rfbEncodingZRLE; }
	bool request_compress_level() const override { return true; }

	boost::asio::awaitable<error> decode(vnc_stream_type &socket, const proto::rfbRectangle &rect,
					     frame_buffer &frame, std::shared_ptr<frame_op> op) noexcept override
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

		bool fitsInLS3Bytes = maxColor < (1 << 24);
		bool fitsInMS3Bytes = (maxColor & 0xff) == 0;
		bool isLowCPixel =
			(format.bytes_per_pixel() == 4) && (format.depth.value() <= 24) &&
			((fitsInLS3Bytes && !format.bigEndian.value()) || (fitsInMS3Bytes && format.bigEndian.value()));
		bool isHighCPixel =
			(format.bytes_per_pixel() == 4) && (format.depth.value() <= 24) &&
			((fitsInLS3Bytes && format.bigEndian.value()) || (fitsInMS3Bytes && !format.bigEndian.value()));

		int min_buffer_size = rw * rh * sizeof(uint32_t) * 2;

		auto d_buffer = decompress_buffer_.prepare(min_buffer_size);

		/* Need to initialize the decompressor state. */
		decompStream.next_in = (Bytef *)buffer_.data();
		decompStream.avail_in = 0;
		decompStream.next_out = (Bytef *)d_buffer.data();
		decompStream.avail_out = d_buffer.size();
		decompStream.data_type = Z_BINARY;

		int inflateResult;
		/* Initialize the decompression stream structures on the first invocation. */
		if (decompStreamInited == FALSE) {

			inflateResult = inflateInit(&decompStream);

			if (inflateResult != Z_OK) {
				co_return error::make_error(custom_error::frame_error,
							    fmt::format("inflateInit returned error: {}, msg: {}",
									inflateResult, decompStream.msg));
			}

			decompStreamInited = TRUE;
		}
		inflateResult = Z_OK;

		for (; remaining > 0;) {
			auto bytes = co_await boost::asio::async_read(
				socket, boost::asio::buffer(buffer_, std::min<std::size_t>(remaining, buffer_.size())),
				net_awaitable[ec]);
			if (ec)
				co_return error::make_error(ec);

			decompStream.next_in = (Bytef *)buffer_.data();
			decompStream.avail_in = bytes;

			/* Need to uncompress buffer full. */
			inflateResult = inflate(&decompStream, Z_SYNC_FLUSH);

			/* We never supply a dictionary for compression. */
			if (inflateResult == Z_NEED_DICT) {
				co_return error::make_error(custom_error::frame_error,
							    "zlib inflate needs a dictionary!");
				//return FALSE;
			}
			if (inflateResult < 0) {
				co_return error::make_error(custom_error::frame_error,
							    fmt::format("zlib inflate returned error: {}, msg: {}",
									inflateResult, decompStream.msg));
			}

			remaining -= bytes;
		}
		if (inflateResult != Z_OK) {
			co_return error::make_error(custom_error::frame_error,
						    fmt::format("zlib inflate returned error: {}, msg: {}",
								inflateResult, decompStream.msg));
		}
		remaining = min_buffer_size - decompStream.avail_out;

		decompress_buffer_.commit(remaining);
		if (decompress_buffer_.size() == 0) {
			spdlog::error("11111111111111111111111111");
		}

		int total_size = decompress_buffer_.size();

		std::istream is(&decompress_buffer_);

		for (int j = 0; j < rh; j += rfbZRLETileHeight) {
			for (int i = 0; i < rw; i += rfbZRLETileWidth) {
				int subWidth = (i + rfbZRLETileWidth > rw) ? rw - i : rfbZRLETileWidth;
				int subHeight = (j + rfbZRLETileHeight > rh) ? rh - j : rfbZRLETileHeight;

				if (format.bytes_per_pixel() == 1) {
					handle_zrle_tile<uint8_t>(frame, is, isLowCPixel, isHighCPixel, rx + i, ry + j,
								  subWidth, subHeight);
				} else if (format.bytes_per_pixel() == 2) {
					handle_zrle_tile<uint16_t>(frame, is, isLowCPixel, isHighCPixel, rx + i, ry + j,
								   subWidth, subHeight);
				} else if (format.bytes_per_pixel() == 4) {
					handle_zrle_tile<uint32_t>(frame, is, isLowCPixel, isHighCPixel, rx + i, ry + j,
								   subWidth, subHeight);
				}
			}
		}
		remaining = decompress_buffer_.size();
		auto use_size = total_size - remaining;
		if (remaining != 0) {
			spdlog::error("222222222222222222222222");
		}
		decompress_buffer_.consume(decompress_buffer_.size());
		co_return error{};

		//z_stream_.flush();
		//z_stream_.flush();

		//z_stream_.push(boost::iostreams::zlib_decompressor());
		//z_stream_.push(decompress_buffer_);
		//zstr::istream z_is(&buffer_, zstr::default_buff_size, false, 15);
		//bits_reader reader(*z_is_, real_bpp, shift);

		//int min_buffer_size = rw * rh * (real_bpp / 8) * 2;
		//auto buffer = decompress_buffer_.prepare(min_buffer_size);
		//decompress_buffer_.resize(min_buffer_size);
		//z_is_->read((char *)buffer.data(), buffer.size());
		//int read_count = z_is_->gcount();
		/*if (buffer_.size() != 0)
			co_return error::make_error(custom_error::frame_error, "zrle zlib error");*/

		//decompress_buffer_.commit(read_count);

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
	z_stream decompStream = {0};
	bool decompStreamInited = false;

	std::array<uint8_t, 640 * 480> buffer_;
	boost::asio::streambuf decompress_buffer_;
};
} // namespace libvnc::encoding