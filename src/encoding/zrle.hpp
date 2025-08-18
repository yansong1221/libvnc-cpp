#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <zstr.hpp>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/device/back_inserter.hpp>

namespace libvnc::encoding {

class zrle : public frame_codec {

	constexpr static auto rfbZRLETileWidth = 64;
	constexpr static auto rfbZRLETileHeight = 64;

	class bits_reader {
	public:
		bits_reader(std::istream &is, int bpp, int shift = 0) : is_(is), bpp_(bpp), shift_(shift) {}

		uint32_t read() { return read(bpp_, shift_); }
		uint32_t read(int bpp, int shift = 0)
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

			if (shift < 0)
				result <<= -shift;
			else if (shift > 0)
				result >>= shift;

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
		int shift_;
		int bpp_;
		int bit_pos_ = 8;
		uint8_t cur_byte_ = 0;
	};

public:
	void init() override
	{
		buffer_.consume(buffer_.size());
		z_is_ = std::make_unique<zstr::istream>(&buffer_, zstr::default_buff_size, false, 15);
	}
	std::string codec_name() const override { return "zrle"; }
	proto::rfbEncoding encoding_code() const override { return proto::rfbEncodingZRLE; }
	bool request_compress_level() const override { return true; }

	boost::asio::awaitable<error> decode(vnc_stream_type &socket, const proto::rfbRectangle &rect,
					     frame_buffer &frame, std::shared_ptr<frame_op> op) noexcept override
	{
		if (auto err = co_await frame_codec::decode(socket, rect, frame, op); err)
			co_return err;

		//Down + >> 8bit
		//Up - << 8bit

		int rx = rect.x.value();
		int ry = rect.y.value();
		int rw = rect.w.value();
		int rh = rect.h.value();

		auto format = frame.pixel_format();

		auto bpp = format.bitsPerPixel.value();
		auto real_bpp = bpp;
		int shift = 0;

		if (bpp == 16 && format.greenMax.value() <= 0x1F)
			real_bpp = 15;
		if (bpp == 32) {
			uint32_t maxColor = (format.redMax.value() << format.redShift.value()) |
					    (format.greenMax.value() << format.greenShift.value()) |
					    (format.blueMax.value() << format.blueShift.value());

			if ((format.bigEndian.value() && (maxColor & 0xff) == 0) ||
			    (!format.bigEndian.value() && (maxColor & 0xff000000) == 0)) {
				real_bpp = 24;
			} else if (!format.bigEndian.value() && (maxColor & 0xff) == 0) {
				real_bpp = 24;
				shift = -8;
				//左移8位
			} else if (format.bigEndian.value() && (maxColor & 0xff000000) == 0) {
				real_bpp = 24;
				shift = 8;
				//右移8位
			}
		}
		boost::system::error_code ec;
		boost::endian::big_uint32_buf_t nBytes{};

		co_await boost::asio::async_read(socket, boost::asio::buffer(&nBytes, sizeof(nBytes)),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		auto total_size = nBytes.value();

		co_await boost::asio::async_read(socket, buffer_, boost::asio::transfer_exactly(total_size),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		//zstr::istream z_is(&buffer_, zstr::default_buff_size, false, 15);
		bits_reader reader(*z_is_, real_bpp, shift);

		int min_buffer_size = rw * rh * (real_bpp / 8) * 2;
		//decompress_buffer_.resize(min_buffer_size);
		//z_is_->read((char *)decompress_buffer_.data(), decompress_buffer_.size());

		for (int j = 0; j < rh; j += rfbZRLETileHeight) {
			for (int i = 0; i < rw; i += rfbZRLETileWidth) {
				int subWidth = (i + rfbZRLETileWidth > rw) ? rw - i : rfbZRLETileWidth;
				int subHeight = (j + rfbZRLETileHeight > rh) ? rh - j : rfbZRLETileHeight;

				handle_zrle_tile(frame, reader, rx + i, ry + j, subWidth, subHeight); 
			}
		}

		//if (buffer_.size() != 0)
		//co_return error::make_error(custom_error::frame_error, "zlib error");
		buffer_.consume(total_size);
		co_return error{};
	}
	void handle_zrle_tile(frame_buffer &frame, bits_reader &reader, int rx, int ry, int rw, int rh) noexcept
	{
		auto bbp = frame.pixel_format().bitsPerPixel.value();

		uint8_t type = reader.read_byte();
		if (type == 0) /* raw */ {
			for (int y = 0; y < rh; ++y) {
				for (int x = 0; x < rw; ++x) {
					uint32_t val = reader.read();
					frame.got_bitmap((uint8_t *)&val, rx + x, ry + y, 1, 1);
				}
			}
		} else if (type == 1) {
			uint32_t val = reader.read();
			frame.fill_rect(rx, ry, rw, ry, (uint8_t *)&val);
		} else if (type <= 127) /* packed Palette */
		{
			uint32_t palette[128] = {0};
			for (int i = 0; i < type; ++i) {
				palette[i] = reader.read();
			}

			int bpp = (type > 4 ? (type > 16 ? 8 : 4) : (type > 2 ? 2 : 1));

			for (int y = 0; y < rh; ++y) {
				for (int x = 0; x < rw; ++x) {
					uint32_t index = reader.read(bpp, 0);
					frame.got_bitmap((uint8_t *)&palette[index], rx + x, ry + y, 1, 1);
				}
			}
		} else if (type == 128) {
			int i = 0, j = 0;
			while (j < rh) {

				uint32_t color = reader.read();
				int length = 1;

				while (true) {
					auto val = reader.read_byte();
					if (val == 0xff) {
						length += val;
						break;
					}
					length += val;
				}
				while (j < rh && length > 0) {
					frame.got_bitmap((uint8_t *)&color, rx + i, ry + j, 1, 1);
					length--;
					i++;
					if (i >= rw) {
						i = 0;
						j++;
					}
				}
			}
		} else if (type >= 130) {
			uint32_t palette[128] = {0};
			for (int i = 0; i < type - 128; i++) {
				palette[i] = reader.read();
			}
			int i = 0, j = 0;
			while (j < rh) {

				int val = reader.read_byte();

				uint32_t color = val & 0x7f;
				int length = 1;

				if (val & 0x80) {
					while (true) {
						auto val = reader.read_byte();
						if (val == 0xff) {
							length += val;
							break;
						}
						length += val;
					}
				}

				while (j < rh && length > 0) {
					frame.got_bitmap((uint8_t *)&color, rx + i, ry + j, 1, 1);
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

private:
	boost::asio::streambuf buffer_;
	std::unique_ptr<zstr::istream> z_is_;
	std::vector<uint8_t> decompress_buffer_;
};
} // namespace libvnc::encoding