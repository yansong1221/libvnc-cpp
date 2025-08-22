#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <turbojpeg.h>
#include <zstr.hpp>
#include "helper.hpp"

namespace libvnc::encoding {

namespace detail {

static bool is_argb32_with_tight_rgb24(const proto::rfbPixelFormat &format)
{
	auto bitsPerPixel = format.bitsPerPixel.value();

	if (bitsPerPixel == 32 && format.depth.value() == 24 && format.redMax.value() == 0xFF &&
	    format.greenMax.value() == 0xFF && format.blueMax.value() == 0xFF)
		return true;

	return false;
}
static int tight_bits_per_pixel(const proto::rfbPixelFormat &format)
{
	if (is_argb32_with_tight_rgb24(format))
		return 24;

	return format.bitsPerPixel.value();
}

static boost::asio::awaitable<error> read_compact_len(vnc_stream_type &socket, long &len) noexcept
{
	try {
		uint8_t b;
		len = 0;
		co_await boost::asio::async_read(socket, boost::asio::buffer(&b, sizeof(b)));
		len = (int)b & 0x7F;
		if (b & 0x80) {
			co_await boost::asio::async_read(socket, boost::asio::buffer(&b, sizeof(b)));
			len |= ((int)b & 0x7F) << 7;
			if (b & 0x80) {
				co_await boost::asio::async_read(socket, boost::asio::buffer(&b, sizeof(b)));
				len |= ((int)b & 0xFF) << 14;
			}
		}
		if (len <= 0) {
			co_return error::make_error(custom_error::frame_error,
						    "Incorrect data received from the server.");
		}
		co_return error{};
	} catch (const boost::system::system_error &e) {
		co_return error::make_error(e.code());
	}
}
class filter {
public:
	virtual ~filter() = default;

	virtual boost::asio::awaitable<error> init_filter(vnc_stream_type &socket, const proto::rfbPixelFormat &format,
							  int &bitsPixel) = 0;
	virtual boost::asio::awaitable<error> proc_filter(boost::asio::const_buffer decompress_data,
							  const proto::rfbRectangle &rect, frame_buffer &frame) = 0;
};

class copy_filter : public filter {
public:
	boost::asio::awaitable<error> init_filter(vnc_stream_type &socket, const proto::rfbPixelFormat &format,
						  int &bitsPerPixel) override
	{
		bitsPerPixel = detail::tight_bits_per_pixel(format);
		co_return error{};
	}
	boost::asio::awaitable<error> proc_filter(boost::asio::const_buffer decompress_data,
						  const proto::rfbRectangle &rect, frame_buffer &frame) override
	{
		int rx = rect.x.value();
		int ry = rect.y.value();
		int rw = rect.w.value();
		int rh = rect.h.value();

		auto format = frame.pixel_format();
		if (is_argb32_with_tight_rgb24(format)) {
			buffer_.resize(decompress_data.size());
			boost::asio::buffer_copy(boost::asio::buffer(buffer_), decompress_data);
			if (auto err = helper::rgb24_to_pixel<uint32_t>(format, buffer_); err)
				co_return err;

			decompress_data = boost::asio::buffer(buffer_);
		}

		auto row_bytes = rw * frame.bytes_per_pixel();
		for (int i = 0; i < rh; ++i) {
			auto ptr = frame.data(rx, ry + i);
			std::memcpy(ptr, decompress_data.data(), row_bytes);
			decompress_data += row_bytes;
		}
		co_return error{};
	}

private:
	std::vector<uint8_t> buffer_;
};

class palette_filter : public filter {
	class index_reader {
	public:
		virtual ~index_reader() = default;
		virtual void set(const std::span<const uint8_t> &bytes) = 0;
		virtual std::optional<int> next() = 0;
	};

	class bytes_reader : public index_reader {
	public:
		void set(const std::span<const uint8_t> &bytes) override
		{
			bytes_ = bytes;
			bytePos_ = 0;
		}
		std::optional<int> next() override
		{
			if (bytePos_ >= bytes_.size())
				return std::nullopt; // EOF

			return bytes_[bytePos_++];
		}

	private:
		std::span<const uint8_t> bytes_;
		size_t bytePos_ = 0;
	};

	class bits_reader : public index_reader {
	public:
		void set(const std::span<const uint8_t> &bytes) override
		{
			bytes_ = bytes;
			bytePos_ = 0;
			bitPos_ = 0;
		}
		std::optional<int> next() override
		{
			if (bytePos_ >= bytes_.size())
				return std::nullopt; // EOF

			int bit = (bytes_[bytePos_] >> (7 - bitPos_)) & 1;
			if (++bitPos_ == 8) {
				bitPos_ = 0;
				++bytePos_;
			}
			return bit;
		}

	private:
		std::span<const uint8_t> bytes_;
		size_t bytePos_ = 0;
		int bitPos_ = 0;
	};

public:
	boost::asio::awaitable<error> init_filter(vnc_stream_type &socket, const proto::rfbPixelFormat &format,
						  int &bitsPerPixel) override
	{
		bitsPerPixel = detail::tight_bits_per_pixel(format);

		boost::system::error_code ec;
		co_await boost::asio::async_read(socket, boost::asio::buffer(&rectColors_, sizeof(rectColors_)),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		++rectColors_;
		auto bytesPixel = bitsPerPixel / 8;

		tightPalette_.resize(rectColors_ * bytesPixel);

		co_await boost::asio::async_read(socket, boost::asio::buffer(tightPalette_), net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		if (is_argb32_with_tight_rgb24(format)) {
			if (auto err = helper::rgb24_to_pixel<uint32_t>(format, tightPalette_); err)
				co_return err;
		}

		bitsPerPixel = rectColors_ == 2 ? 1 : 8;
		co_return error{};
	}
	boost::asio::awaitable<error> proc_filter(boost::asio::const_buffer decompress_data,
						  const proto::rfbRectangle &rect, frame_buffer &frame) override
	{
		int rx = rect.x.value();
		int ry = rect.y.value();
		int rw = rect.w.value();
		int rh = rect.h.value();

		auto bytes_per_pixel = frame.bytes_per_pixel();

		index_reader *reader = &bytes_reader_;
		if (rectColors_ == 2)
			reader = &bits_reader_;

		reader->set({(const uint8_t *)decompress_data.data(), decompress_data.size()});

		for (int y = 0; y < rh; y++) {
			for (int x = 0; x < rw; x++) {
				uint8_t *dst = frame.data(rx + x, ry + y);
				auto index = *reader->next();
				auto value = index_palette(index, bytes_per_pixel);
				std::memcpy(dst, value, bytes_per_pixel);
			}
		}
		co_return error{};
	}
	const uint8_t *index_palette(int index, int bytes_per_pixel) const
	{
		return &tightPalette_[index * bytes_per_pixel];
	}

private:
	uint8_t rectColors_ = 0;
	std::vector<uint8_t> tightPalette_;
	bytes_reader bytes_reader_;
	bits_reader bits_reader_;
};

class gradient_filter : public filter {
public:
	boost::asio::awaitable<error> init_filter(vnc_stream_type &socket, const proto::rfbPixelFormat &format,
						  int &bitsPerPixel) override
	{
		bitsPerPixel = detail::tight_bits_per_pixel(format);
		co_return error{};
	}
	boost::asio::awaitable<error> proc_filter(boost::asio::const_buffer decompress_data,
						  const proto::rfbRectangle &rect, frame_buffer &frame) override
	{
		int rx = rect.x.value();
		int ry = rect.y.value();
		int rw = rect.w.value();
		int rh = rect.h.value();

		co_return error{};
	}
};

} // namespace detail

class tight : public frame_codec {
	constexpr static auto rfbTightExplicitFilter = 0x04;
	constexpr static auto rfbTightFill = 0x08;
	constexpr static auto rfbTightJpeg = 0x09;
	constexpr static auto rfbTightNoZlib = 0x0A;
	constexpr static auto rfbTightPng = 0x0A;
	constexpr static auto rfbTightMaxSubencoding = 0x0A;

	/* Filters to improve compression efficiency */
	constexpr static auto rfbTightFilterCopy = 0x00;
	constexpr static auto rfbTightFilterPalette = 0x01;
	constexpr static auto rfbTightFilterGradient = 0x02;

	constexpr static auto TIGHT_MIN_TO_COMPRESS = 12;

public:
	void init() override
	{
		buffer_.consume(buffer_.size());
		jpeg_handle_.reset();
		z_streams_[0] = std::make_unique<zstr::istream>(&buffer_, zstr::default_buff_size, false, 15);
		z_streams_[1] = std::make_unique<zstr::istream>(&buffer_, zstr::default_buff_size, false, 15);
		z_streams_[2] = std::make_unique<zstr::istream>(&buffer_, zstr::default_buff_size, false, 15);
		z_streams_[3] = std::make_unique<zstr::istream>(&buffer_, zstr::default_buff_size, false, 15);
	}
	std::string codec_name() const override { return "tight"; }
	proto::rfbEncoding encoding_code() const override { return proto::rfbEncodingTight; }

	boost::asio::awaitable<error> decode(vnc_stream_type &socket, const proto::rfbRectangle &rect,
					     frame_buffer &buffer, std::shared_ptr<client_op> op) override
	{
		if (auto err = co_await frame_codec::decode(socket, rect, buffer, op); err)
			co_return err;

		int rx = rect.x.value();
		int ry = rect.y.value();
		int rw = rect.w.value();
		int rh = rect.h.value();

		boost::system::error_code ec;

		uint8_t comp_ctl = 0;
		co_await boost::asio::async_read(socket, boost::asio::buffer(&comp_ctl, sizeof(comp_ctl)),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		/* Flush zlib streams if we are told by the server to do so. */
		for (int stream_id = 0; stream_id < 4; stream_id++) {
			if ((comp_ctl & 1)) {
				z_streams_[stream_id] =
					std::make_unique<zstr::istream>(&buffer_, zstr::default_buff_size, false, 15);
			}
			comp_ctl >>= 1;
		}

		bool readUncompressed = false;
		if ((comp_ctl & rfbTightNoZlib) == rfbTightNoZlib) {
			comp_ctl &= ~(rfbTightNoZlib);
			readUncompressed = true;
		}
		if (comp_ctl == rfbTightFill)
			co_return co_await tight_fill(socket, rect, buffer);
		else if (comp_ctl == rfbTightJpeg)
			co_return co_await tight_jpeg(socket, rect, buffer);
		else if (comp_ctl > rfbTightMaxSubencoding) {
			co_return error::make_error(custom_error::frame_error,
						    "Tight encoding: bad subencoding value received.");
		}

		/*
         * Here primary compression mode handling begins.
         * Data was processed with optional filter + zlib compression.
         */

		/* First, we should identify a filter to use. */
		detail::filter *_filter = nullptr;
		if ((comp_ctl & rfbTightExplicitFilter) != 0) {
			uint8_t filter_id = 0;
			co_await boost::asio::async_read(socket, boost::asio::buffer(&filter_id, sizeof(filter_id)),
							 net_awaitable[ec]);
			if (ec)
				co_return error::make_error(ec);

			switch (filter_id) {
			case rfbTightFilterCopy: {
				_filter = &copy_filter_;
			} break;
			case rfbTightFilterPalette: {
				_filter = &palette_filter_;

			} break;
			case rfbTightFilterGradient: {
				_filter = &gradient_filter_;

			} break;
			}
		} else
			_filter = &copy_filter_;

		if (_filter == nullptr) {
			co_return error::make_error(custom_error::frame_error,
						    "Tight encoding: unknown filter code received.");
		}

		int bitsPixel = 0;
		if (auto err = co_await _filter->init_filter(socket, buffer.pixel_format(), bitsPixel); err)
			co_return err;

		/* Determine if the data should be decompressed or just copied. */
		int rowSize = (rw * bitsPixel + 7) / 8;
		int allBytes = rh * rowSize;
		if (allBytes < TIGHT_MIN_TO_COMPRESS) {
			co_await boost::asio::async_read(socket, buffer_, boost::asio::transfer_exactly(allBytes),
							 net_awaitable[ec]);
			if (ec)
				co_return error::make_error(ec);

			if (auto err = co_await _filter->proc_filter(buffer_.data(), rect, buffer); err)
				co_return err;

			buffer_.consume(allBytes);
			co_return error{};
		}

		/* Read the length (1..3 bytes) of compressed data following. */
		long compressedLen = 0;
		if (auto err = co_await detail::read_compact_len(socket, compressedLen); err)
			co_return err;

		co_await boost::asio::async_read(socket, buffer_, boost::asio::transfer_exactly(compressedLen),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		if (readUncompressed) {
			if (auto err = co_await _filter->proc_filter(buffer_.data(), rect, buffer); err)
				co_return err;

			buffer_.consume(compressedLen);
			co_return error{};
		}
		/* Now let's initialize compression stream if needed. */
		int stream_id = comp_ctl & 0x03;

		decompress_buffer_.resize(allBytes);
		z_streams_[stream_id]->read((char *)decompress_buffer_.data(), allBytes);

		if (z_streams_[stream_id]->gcount() != allBytes) {
			co_return error::make_error(custom_error::frame_error, "Tight zlib error.");
		}

		if (auto err = co_await _filter->proc_filter(boost::asio::buffer(decompress_buffer_), rect, buffer);
		    err)
			co_return err;

		co_return error{};
	}

private:
	boost::asio::awaitable<error> tight_fill(vnc_stream_type &socket, const proto::rfbRectangle &rect,
						 frame_buffer &frame)
	{

		int rx = rect.x.value();
		int ry = rect.y.value();
		int rw = rect.w.value();
		int rh = rect.h.value();

		auto format = frame.pixel_format();

		auto bytes_pixel = detail::tight_bits_per_pixel(format) / 8;

		boost::system::error_code ec;
		std::vector<uint8_t> fill_colour;
		fill_colour.resize(bytes_pixel);

		co_await boost::asio::async_read(socket, boost::asio::buffer(fill_colour, bytes_pixel),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		if (detail::is_argb32_with_tight_rgb24(format))
			helper::rgb24_to_pixel<uint32_t>(format, fill_colour);

		frame.fill_rect(rx, ry, rw, rh, fill_colour.data());
		co_return error{};
	}
	boost::asio::awaitable<error> tight_jpeg(vnc_stream_type &socket, const proto::rfbRectangle &rect,
						 frame_buffer &frame)
	{
		auto format = frame.pixel_format();
		auto bytes_pixel = frame.bytes_per_pixel();

		int rx = rect.x.value();
		int ry = rect.y.value();
		int rw = rect.w.value();
		int rh = rect.h.value();

		if (bytes_pixel == 1)
			co_return error::make_error(custom_error::frame_error,
						    "Tight encoding: JPEG is not supported in 8 bpp mode.");

		long compressedLen = 0;
		if (auto err = co_await detail::read_compact_len(socket, compressedLen); err)
			co_return err;

		boost::system::error_code ec;
		co_await boost::asio::async_read(socket, buffer_, boost::asio::transfer_exactly(compressedLen),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		if (!jpeg_handle_)
			jpeg_handle_.reset(tjInitDecompress());

		uint8_t *dst = nullptr;
		int pixelSize, pitch, flags = 0;
		if (bytes_pixel == 2) {
			flags = 0;
			pixelSize = 3;
			pitch = rw * pixelSize;
			decompress_buffer_.resize(pitch * rh);
			dst = decompress_buffer_.data();
		} else {
			if (format.bigEndian.value())
				flags |= TJ_ALPHAFIRST;
			if (format.redShift.value() == 16 && format.blueShift.value() == 0)
				flags |= TJ_BGR;
			if (format.bigEndian.value())
				flags ^= TJ_BGR;
			pixelSize = bytes_pixel;
			pitch = frame.width() * pixelSize;
			dst = frame.data() + (ry * pitch + rx * pixelSize);
		}

		if (tjDecompress(jpeg_handle_.get(), (uint8_t *)buffer_.data().data(), (unsigned long)compressedLen,
				 dst, rw, pitch, rh, pixelSize, flags) == -1) {
			co_return error::make_error(custom_error::frame_error,
						    fmt::format("TurboJPEG error: {}", tjGetErrorStr()));
		}

		if (bytes_pixel == 2) {
			auto err = helper::rgb24_to_pixel<uint16_t>(format, decompress_buffer_);
			if (err)
				co_return err;
			frame.got_bitmap(decompress_buffer_.data(), rx, ry, rw, rh);
		}
		buffer_.consume(compressedLen);
		co_return error{};
	}

private:
	boost::asio::streambuf buffer_;
	std::vector<uint8_t> decompress_buffer_;
	std::array<std::unique_ptr<zstr::istream>, 4> z_streams_;

	struct tjhandle_deleter {
		void operator()(tjhandle handle)
		{
			if (handle)
				tjDestroy(handle);
		}
	};

	std::unique_ptr<std::remove_pointer_t<tjhandle>, tjhandle_deleter> jpeg_handle_;
	detail::copy_filter copy_filter_;
	detail::palette_filter palette_filter_;
	detail::gradient_filter gradient_filter_;
};
} // namespace libvnc::encoding