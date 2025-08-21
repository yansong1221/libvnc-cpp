#pragma once
#include "use_awaitable.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace libvnc {

struct crypto_provider {
      virtual ~crypto_provider() = default;
      virtual std::vector<std::uint8_t> encrypt(std::span<const std::uint8_t> plain) = 0;
      virtual std::vector<std::uint8_t> decrypt(std::span<const std::uint8_t> data) = 0;
};

template <typename... T>
class variant_stream : public std::variant<T...> {
   public:
      using std::variant<T...>::variant;

   public:
      using executor_type = boost::asio::any_io_executor;
      using lowest_layer_type = boost::asio::ip::tcp::socket::lowest_layer_type;

      executor_type get_executor() {
         return std::visit([&](auto& t) mutable { return t.get_executor(); }, *this);
      }

      lowest_layer_type& lowest_layer() {
         return std::visit([&](auto& t) mutable -> lowest_layer_type& { return t.lowest_layer(); }, *this);
      }

      const lowest_layer_type& lowest_layer() const {
         return std::visit([&](auto& t) mutable -> const lowest_layer_type& { return t.lowest_layer(); }, *this);
      }

      template <typename MutableBufferSequence, typename ReadHandler>
      auto async_read_some(const MutableBufferSequence& buffers, ReadHandler&& handler) {
         return std::visit(
            [&, handler = std::move(handler)](auto& t) mutable {
               if(!provider_)
                  return t.async_read_some(buffers, std::forward<ReadHandler>(handler));

               return boost::asio::async_initiate<ReadHandler, void(boost::system::error_code, std::size_t)>(
                  [this, &t, buffers](auto&& completion_handler) mutable {
                     using namespace boost::asio;

                     co_spawn(
                        t.get_executor(),
                        [this, &t, completion_handler = std::move(completion_handler), buffers]() mutable
                           -> awaitable<void> {
                           try {
                              if(read_remaining_buffer_.size() > 0) {
                                 boost::asio::post(
                                    t.get_executor(),
                                    [this, &t, completion_handler = std::move(completion_handler), buffers]() mutable {
                                       std::size_t bytes_written = boost::asio::buffer_copy(
                                          buffers, boost::asio::buffer(read_remaining_buffer_.data()));
                                       read_remaining_buffer_.consume(bytes_written);
                                       completion_handler(make_error_code(boost::system::errc::success), bytes_written);
                                    });
                                 co_return;
                              }

                              for(;;) {
                                 boost::system::error_code ec;
                                 auto bytes_read = co_await t.async_read_some(buffers, net_awaitable[ec]);
                                 if(ec) {
                                    completion_handler(ec, 0);
                                    co_return;
                                 }
                                 auto decrypted_data = provider_->decrypt({(const uint8_t*)buffers.data(), bytes_read});
                                 if(decrypted_data.empty())
                                    continue;

                                 std::size_t bytes_written =
                                    boost::asio::buffer_copy(buffers, boost::asio::buffer(decrypted_data));

                                 if(auto remaining_size = decrypted_data.size() - bytes_written; remaining_size > 0) {
                                    boost::asio::buffer_copy(
                                       read_remaining_buffer_.prepare(remaining_size),
                                       boost::asio::buffer(decrypted_data.data() + bytes_written, remaining_size));
                                    read_remaining_buffer_.commit(remaining_size);
                                 }

                                 completion_handler(ec, bytes_written);
                                 co_return;
                              }

                           } catch(...) {
                              completion_handler(make_error_code(boost::system::errc::io_error), 0);
                           }
                           co_return;
                        },
                        detached);
                  },
                  handler);
            },
            *this);
      }

      template <typename ConstBufferSequence, typename WriteHandler>
      auto async_write_some(const ConstBufferSequence& buffers, WriteHandler&& handler) {
         return std::visit(
            [&, handler = std::move(handler)](auto& t) mutable {
               if(!provider_)
                  return t.async_write_some(buffers, std::forward<WriteHandler>(handler));

               using namespace boost::asio;

               return async_initiate<WriteHandler, void(boost::system::error_code, std::size_t)>(
                  [this, &t, buffers](auto&& completion_handler) mutable {
                     co_spawn(
                        t.get_executor(),
                        [this, &t, completion_handler = std::move(completion_handler), buffers]() mutable
                           -> awaitable<void> {
                           try {
                              auto encrypted_data =
                                 provider_->encrypt({(const uint8_t*)buffers.data(), buffers.size()});

                              boost::system::error_code ec;
                              co_await boost::asio::async_write(t, buffer(encrypted_data), net_awaitable[ec]);
                              if(ec) {
                                 completion_handler(ec, 0);
                                 co_return;
                              }
                              completion_handler(ec, buffers.size());
                           } catch(...) {
                              completion_handler(make_error_code(boost::system::errc::io_error), 0);
                           }
                        },
                        detached);
                  },
                  handler);
            },
            *this);
      }

      boost::asio::ip::tcp::endpoint remote_endpoint() { return lowest_layer().remote_endpoint(); }

      boost::asio::ip::tcp::endpoint remote_endpoint(boost::system::error_code& ec) {
         return lowest_layer().remote_endpoint(ec);
      }

      boost::asio::ip::tcp::endpoint local_endpoint() { return lowest_layer().local_endpoint(); }

      boost::asio::ip::tcp::endpoint local_endpoint(boost::system::error_code& ec) {
         return lowest_layer().local_endpoint(ec);
      }

      void shutdown(boost::asio::socket_base::shutdown_type what, boost::system::error_code& ec) {
         lowest_layer().shutdown(what, ec);
      }

      bool is_open() const { return lowest_layer().is_open(); }

      void close(boost::system::error_code& ec) { lowest_layer().close(ec); }

      void set_provider(std::unique_ptr<crypto_provider>&& provider) { provider_ = std::move(provider); }

   private:
      std::unique_ptr<crypto_provider> provider_;
      boost::asio::streambuf read_remaining_buffer_;
};

}  // namespace libvnc