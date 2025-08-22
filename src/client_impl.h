#pragma once
#include "client_delegate_proxy.hpp"
#include "encoding/encoding.h"
#include "libvnc-cpp/client.h"
#include "libvnc-cpp/error.h"
#include "libvnc-cpp/proto.h"
#include "spdlog/spdlog.h"
#include "stream/stream.hpp"
#include "supported_messages.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/strand.hpp>
#include <map>
#include <queue>
#include <set>
#include <span>

namespace libvnc {

class client_impl : public encoding::client_op {
   public:
      client_impl(const boost::asio::any_io_executor& executor);

   public:
      const frame_buffer& frame() const;

      void close();
      void start();

      bool send_format(const proto::rfbPixelFormat& format);
      bool send_frame_encodings(const std::vector<std::string>& encodings);
      bool send_scale_setting(int scale);
      bool send_ext_desktop_size(const std::vector<proto::rfbExtDesktopScreen>& screens);
      bool send_key_event(uint32_t key, bool down);
      bool send_extended_key_event(uint32_t keysym, uint32_t keycode, bool down);
      bool send_pointer_event(int x, int y, int buttonMask);
      bool send_client_cut_text(std::string_view text);
      bool send_client_cut_text_utf8(std::string_view text);

      bool text_chat_send(std::string_view text);
      bool text_chat_open();
      bool text_chat_close();
      bool text_chat_finish();

      bool permit_server_input(bool enabled);

      bool send_xvp_msg(uint8_t version, proto::rfbXvpCode code);

      bool send_set_monitor(uint8_t nbr);

      void send_framebuffer_update_request(int x, int y, int w, int h, bool incremental);
      void send_framebuffer_update_request(bool incremental) override;

      std::vector<std::string> supported_frame_encodings() const;

   private:
      boost::asio::awaitable<error> co_run();
      boost::asio::awaitable<error> async_connect_rfbserver();
      boost::asio::awaitable<error> async_handshake();
      boost::asio::awaitable<error> async_authenticate();
      boost::asio::awaitable<error> async_client_init();
      boost::asio::awaitable<error> async_send_client_init_extra_msg();

      boost::asio::awaitable<error> server_message_loop();
      boost::asio::awaitable<error> server_keepalive_loop();

      boost::asio::awaitable<error> read_auth_result();
      boost::asio::awaitable<error> read_error_reason();

      bool send_msg_to_server(const proto::rfbClientToServerMsg& ID, const void* data, std::size_t len);
      bool send_msg_to_server_buffers(const proto::rfbClientToServerMsg& ID,
                                      const std::vector<boost::asio::const_buffer>& buffers);

      template <typename... Buffers>
      bool send_msg_to_server_buffers(const proto::rfbClientToServerMsg& ID, const Buffers&... buffers) {
         std::vector<boost::asio::const_buffer> bufs;
         (bufs.push_back(boost::asio::buffer(buffers)), ...);
         return send_msg_to_server_buffers(ID, bufs);
      }

      void send_raw_data(std::vector<uint8_t>&& data);
      void commit_status(const client::status& s);

      proto::rfbAuthScheme select_auth_scheme(const std::set<proto::rfbAuthScheme>& auths);

   protected:
      void soft_cursor_lock_area(int x, int y, int w, int h) override;
      void got_cursor_shape(int xhot,
                            int yhot,
                            const frame_buffer& rc_source,
                            std::span<const uint8_t> rc_mask) override;
      void handle_cursor_pos(int x, int y) override;
      void handle_keyboard_led_state(int state) override;
      void handle_server_identity(std::string_view text) override;
      void handle_supported_messages(const proto::rfbSupportedMessages& messages) override;
      void handle_ext_desktop_screen(const std::vector<proto::rfbExtDesktopScreen>& screens) override;
      void handle_resize_client_buffer(int width, int height);

   private:
      boost::asio::awaitable<error> on_rfbNoAuth();
      boost::asio::awaitable<error> on_rfbVncAuth();
      boost::asio::awaitable<error> on_rfbUltraVNC();
      boost::asio::awaitable<error> on_rfbUltraMSLogonII();
      boost::asio::awaitable<error> on_rfbClientInitExtraMsgSupport();
      boost::asio::awaitable<error> on_rfbRSAAES_256();
      boost::asio::awaitable<error> on_rfbRSAAESne_256();
      boost::asio::awaitable<error> on_rfbRSAAES();
      boost::asio::awaitable<error> on_rfbRSAAESne();

      boost::asio::awaitable<error> AuthRSAAES(int keySize, bool encrypted);

   private:
      boost::asio::awaitable<error> on_rfbFramebufferUpdate();
      boost::asio::awaitable<error> on_rfbSetColourMapEntries();
      boost::asio::awaitable<error> on_rfbBell();
      boost::asio::awaitable<error> on_rfbServerCutText();
      boost::asio::awaitable<error> on_rfbTextChat();
      boost::asio::awaitable<error> on_rfbXvp();
      boost::asio::awaitable<error> on_rfbResizeFrameBuffer();
      boost::asio::awaitable<error> on_rfbPalmVNCReSizeFrameBuffer();
      boost::asio::awaitable<error> on_rfbMonitorInfo();
      boost::asio::awaitable<error> on_rfbKeepAlive();

   private:
      template <class _Fx, class... _Types>
      void register_message(uint8_t ID, _Fx&& _Func, _Types&&... _Args) {
         message_map_.emplace(ID, std::bind(std::forward<_Fx>(_Func), std::forward<_Types>(_Args)...));
      }

      template <class _Fx, class... _Types>
      void register_auth_message(uint8_t ID, _Fx&& _Func, _Types&&... _Args) {
         auth_message_map_.emplace(ID, std::bind(std::forward<_Fx>(_Func), std::forward<_Types>(_Args)...));
      }

   public:
      boost::asio::strand<boost::asio::any_io_executor> strand_;
      boost::asio::ip::tcp::resolver resolver_;
      socket_stream_ptr stream_;
      std::deque<std::vector<uint8_t>> send_que_;

      std::string host_ = "127.0.0.1";
      uint16_t port_ = 5900;
      bool share_desktop_ = true;
      bool use_ssl_ = false;
      std::string notifiction_text_;

      frame_buffer frame_;

      std::vector<proto::rfbExtDesktopScreen> screens_;

      /** negotiated protocol version */
      int major_ = proto::rfbProtocolMajorVersion, minor_ = proto::rfbProtocolMinorVersion;

      client_delegate_proxy handler_;
      supported_messages supported_messages_;

      encoding::codec_manager codec_manager_;
      using message_handler = std::function<boost::asio::awaitable<error>()>;
      std::map<uint8_t, message_handler> message_map_;
      std::map<uint8_t, message_handler> auth_message_map_;

      std::atomic<client::status> status_ = client::status::closed;
      std::string desktop_name_;
      std::atomic_int current_keyboard_led_state_ = 0;
      std::bitset<32> extendedClipboardServerCapabilities_;
      std::atomic_uint nbrMonitors_ = 0;
      std::atomic_bool ultra_server_ = false;
      std::atomic_bool brfbClientInitExtraMsgSupportNew_ = false;
};

}  // namespace libvnc