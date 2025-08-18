#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>

namespace libvnc::encoding {

class supported_messages : public codec {
public:
	std::string codec_name() const override { return "supported-messages"; }

	void init() override {}
	proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingSupportedMessages; }

	boost::asio::awaitable<error> decode(vnc_stream_type &socket, const proto::rfbRectangle &rect,
					     frame_buffer &buffer, std::shared_ptr<frame_op> op) override
	{
		boost::system::error_code ec;
		proto::rfbSupportedMessages supportedMessages = {0};
		co_await boost::asio::async_read(
			socket, boost::asio::buffer(&supportedMessages, sizeof(supportedMessages)), net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		op->handle_supported_messages(supportedMessages);
		co_return error{};
	}
};
} // namespace libvnc::encoding