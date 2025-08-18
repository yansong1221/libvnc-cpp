#pragma once
#include <type_traits>
#include "libvnc-cpp/proto.h"
#include "libvnc-cpp/error.h"

namespace libvnc::encoding::helper {

template<typename T>
requires std::is_integral_v<T> static T rgb24_to_pixel(const proto::rfbPixelFormat &format, int r, int g, int b)
{
	return ((((T)(r) & 0xFF) * format.redMax.value() + 127) / 255 << format.redShift.value() |
		(((T)(g) & 0xFF) * format.greenMax.value() + 127) / 255 << format.greenShift.value() |
		(((T)(b) & 0xFF) * format.blueMax.value() + 127) / 255 << format.blueShift.value());
}

template<typename T>
requires std::is_integral_v<T> static error rgb24_to_pixel(const proto::rfbPixelFormat &format,
							   std::vector<uint8_t> &rgb24)
{
	if (rgb24.size() % 3 != 0) {
		return error::make_error(custom_error::frame_error, "The input pixel format is not RGB24");
	}
	auto pix_count = rgb24.size() / 3;
	auto bytes_pix = sizeof(T);

	if (bytes_pix > 3)
		rgb24.resize(bytes_pix * pix_count);

	auto src_p = static_cast<const uint8_t *>(rgb24.data());
	auto dst_p = reinterpret_cast<T *>(rgb24.data());

	for (int i = pix_count - 1; i >= 0; i--) {
		uint8_t r = src_p[i * 3];
		uint8_t g = src_p[i * 3 + 1];
		uint8_t b = src_p[i * 3 + 2];
		dst_p[i] = rgb24_to_pixel<T>(format, r, g, b);
	}
	rgb24.resize(bytes_pix * pix_count);
	return error{};
}

} // namespace libvnc::encoding::helper