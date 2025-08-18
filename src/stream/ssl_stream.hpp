#pragma once
#include <memory>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>

namespace libvnc {

template<class NextLayer> struct ssl_stream : public boost::asio::ssl::stream<NextLayer> {
	using boost::asio::ssl::stream<NextLayer>::stream;

	template<typename Arg>
	ssl_stream(Arg &&arg, std::shared_ptr<boost::asio::ssl::context> ctx)
		: boost::asio::ssl::stream<NextLayer>(std::move(arg), *ctx),
		  ssl_ctx_(ctx)
	{
	}

private:
	std::shared_ptr<boost::asio::ssl::context> ssl_ctx_;
};

} // namespace libvnc::stream