#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>

namespace libvnc::encoding {

class keyboard_led_state : public codec {
public:
	void init() override {}
	std::string codec_name() const override { return "keyboard-led-state"; }
	proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingKeyboardLedState; }

	boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket &socket, const proto::rfbRectangle &rect,
					     frame_buffer &buffer, std::shared_ptr<frame_op> op) override
	{
		///* OK! We have received a keyboard state message!!! */
		// client->KeyboardLedStateEnabled = 1;
		// if (client->HandleKeyboardLedState != NULL)
		//     client->HandleKeyboardLedState(client, rect.r.x, 0);
		///* stash it for the future */
		// client->CurrentKeyboardLedState = rect.r.x;
		op->handle_keyboard_led_state(rect.x.value());
		co_return error{};
	}
};
} // namespace libvnc::encoding