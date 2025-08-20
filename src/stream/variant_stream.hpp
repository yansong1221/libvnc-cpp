#pragma once
#include <boost/asio/ip/tcp.hpp>
#include <type_traits>
#include <variant>
#include <vector>

namespace libvnc {

struct crypto_provider {
	virtual ~crypto_provider() = default;
	virtual std::vector<std::uint8_t> encrypt(const std::uint8_t *plain, std::size_t len) = 0;
	virtual std::vector<std::uint8_t> decrypt(const std::uint8_t *data, std::size_t len) = 0;
};

template<typename... T> class variant_stream : public std::variant<T...> {
public:
	using std::variant<T...>::variant;

public:
	using executor_type = boost::asio::any_io_executor;
	using lowest_layer_type = boost::asio::ip::tcp::socket::lowest_layer_type;

	executor_type get_executor()
	{
		return std::visit([&](auto &t) mutable { return t.get_executor(); }, *this);
	}
	lowest_layer_type &lowest_layer()
	{
		return std::visit([&](auto &t) mutable -> lowest_layer_type & { return t.lowest_layer(); }, *this);
	}
	const lowest_layer_type &lowest_layer() const
	{
		return std::visit([&](auto &t) mutable -> const lowest_layer_type & { return t.lowest_layer(); },
				  *this);
	}
	template<typename MutableBufferSequence, typename ReadHandler>
	auto async_read_some(const MutableBufferSequence &buffers, ReadHandler &&handler)
	{
		return std::visit(
			[&, handler = std::move(handler)](auto &t) mutable {
				if (!provider_)
					return t.async_read_some(buffers, std::forward<ReadHandler>(handler));

				return boost::asio::async_initiate<ReadHandler,
								   void(boost::system::error_code, std::size_t)>(
					[this, &t, buffers](auto completion_handler) mutable {
						// 2. 异步读取加密数据
						t.async_read_some(
							boost::asio::buffer(buffers),
							[this, completion_handler = std::move(completion_handler),
							 buffers](boost::system::error_code ec,
								  std::size_t bytes_read) mutable {
								if (ec) {
									completion_handler(ec, 0);
									return;
								}

								try {
									// 3. 解密数据
									auto decrypted_data = provider_->decrypt(
										(uint8_t *)buffers.data(), bytes_read);

									// 4. 将解密后的数据复制到用户缓冲区
									std::size_t bytes_written =
										boost::asio::buffer_copy(
											buffers,
											boost::asio::buffer(
												decrypted_data));

									completion_handler(ec, bytes_written);
								} catch (...) {
									// 5. 处理解密异常
									completion_handler(
										make_error_code(
											boost::system::errc::io_error),
										0);
								}
							});
					},
					handler);
			},
			*this);
	}
	template<typename ConstBufferSequence, typename WriteHandler>
	auto async_write_some(const ConstBufferSequence &buffers, WriteHandler &&handler)
	{
		return std::visit(
			[&, handler = std::move(handler)](auto &t) mutable {
				if (!provider_)
					return t.async_write_some(buffers, std::forward<WriteHandler>(handler));

				// 加密模式：读取用户数据 -> 加密 -> 写入加密数据
				using namespace boost::asio;

				return async_initiate<WriteHandler, void(boost::system::error_code, std::size_t)>(
					[this, &t, buffers](auto&& completion_handler) mutable {
						try {
							// 1. 从用户缓冲区复制数据
							std::vector<uint8_t> plain_data(buffer_size(buffers));
							buffer_copy(buffer(plain_data), buffers);

							// 2. 加密数据
							auto encrypted_data = provider_->encrypt(plain_data.data(),
												 plain_data.size());

							// 3. 异步写入加密数据
							t.async_write_some(buffer(encrypted_data),
									   [encrypted_data = std::move(encrypted_data),
									    completion_handler =
										    std::move(completion_handler)](
										   boost::system::error_code ec,
										   std::size_t bytes) mutable {
										   completion_handler(ec, bytes);
									   });
						} catch (...) {
							// 4. 处理加密异常
							completion_handler(
								make_error_code(boost::system::errc::io_error), 0);
						}
					},
					handler);
			},
			*this);
	}

	boost::asio::ip::tcp::endpoint remote_endpoint() { return lowest_layer().remote_endpoint(); }
	boost::asio::ip::tcp::endpoint remote_endpoint(boost::system::error_code &ec)
	{
		return lowest_layer().remote_endpoint(ec);
	}

	boost::asio::ip::tcp::endpoint local_endpoint() { return lowest_layer().local_endpoint(); }
	boost::asio::ip::tcp::endpoint local_endpoint(boost::system::error_code &ec)
	{
		return lowest_layer().local_endpoint(ec);
	}

	void shutdown(boost::asio::socket_base::shutdown_type what, boost::system::error_code &ec)
	{
		lowest_layer().shutdown(what, ec);
	}

	bool is_open() const { return lowest_layer().is_open(); }

	void close(boost::system::error_code &ec) { lowest_layer().close(ec); }

	void set_provider(std::unique_ptr<crypto_provider> &&provider) { provider_ = std::move(provider); }

private:
	std::unique_ptr<crypto_provider> provider_;
};

} // namespace libvnc