#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>

namespace libvnc::encoding {

class server_identity : public codec {
public:
	std::string codec_name() const override { return "server-identity"; }

	void init() override {}
	proto::rfbEncoding encoding_code() const override { return proto::rfbEncoding::rfbEncodingServerIdentity; }

	boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket &socket, const proto::rfbRectangle &rect,
					     frame_buffer &buffer, std::shared_ptr<frame_op> op) override
	{ /* rect.r.w=byte count */
		boost::system::error_code ec;
		std::string text;
		text.resize(rect.w.value());

		co_await boost::asio::async_read(socket, boost::asio::buffer(text), net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		op->handle_server_identity(text);
		co_return error{};
	}
};
} // namespace libvnc::encoding