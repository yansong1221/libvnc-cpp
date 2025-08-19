#pragma once
#include "libvnc-cpp/error.h"
#include "libvnc-cpp/frame_buffer.h"
#include "libvnc-cpp/proto.h"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <spdlog/spdlog.h>
#include "stream/stream.hpp"

namespace libvnc::encoding {

class frame_op : public std::enable_shared_from_this<frame_op> {
public:
	virtual ~frame_op() = default;
	virtual void soft_cursor_lock_area(int x, int y, int w, int h) = 0;
	virtual void got_cursor_shape(int xhot, int yhot, const frame_buffer &rc_source, const uint8_t *rc_mask) = 0;
	virtual void handle_cursor_pos(int x, int y) = 0;
	virtual void handle_keyboard_led_state(int state) = 0;
	virtual void send_framebuffer_update_request(bool incremental) = 0;
	virtual void handle_server_identity(std::string_view text) = 0;
	virtual void handle_supported_messages(const proto::rfbSupportedMessages &messages) = 0;
	virtual void handle_ext_desktop_screen(const std::vector<proto::rfbExtDesktopScreen> &screens) = 0;
	virtual void handle_resize_client_buffer(int w, int h) = 0;
};

class codec {
public:
	virtual ~codec() = default;
	virtual void init() = 0;
	virtual proto::rfbEncoding encoding_code() const = 0;
	virtual std::string codec_name() const = 0;
	virtual bool is_frame_codec() const { return false; }
	virtual boost::asio::awaitable<error> decode(vnc_stream_type &socket, const proto::rfbRectangle &rect,
						     frame_buffer &buffer, std::shared_ptr<frame_op> op) = 0;
};

class frame_codec : public codec {
public:
	bool is_frame_codec() const final { return true; }
	boost::asio::awaitable<error> decode(vnc_stream_type &socket, const proto::rfbRectangle &rect,
					     frame_buffer &buffer, std::shared_ptr<frame_op> op) override
	{
		if (!buffer.check_rect(rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value())) {
			auto msg = fmt::format("Rect too large: {}x{} at ({}, {})", rect.w.value(), rect.h.value(),
					       rect.x.value(), rect.y.value());
			spdlog::error(msg);
			co_return error::make_error(custom_error::frame_error, msg);
		}
		op->soft_cursor_lock_area(rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
		co_return error{};
	}
};

} // namespace libvnc::encoding
