#pragma once
#include "libvnc-cpp/error.h"
#include "libvnc-cpp/frame_buffer.h"
#include "libvnc-cpp/proto.h"
#include "stream/stream.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace libvnc::encoding {

class client_op : public std::enable_shared_from_this<client_op> {
   public:
      virtual ~client_op() = default;
      virtual void soft_cursor_lock_area(int x, int y, int w, int h) = 0;
      virtual void got_cursor_shape(int xhot,
                                    int yhot,
                                    const frame_buffer& rc_source,
                                    std::span<const uint8_t> rc_mask) = 0;
      virtual void handle_cursor_pos(int x, int y) = 0;
      virtual void handle_keyboard_led_state(int state) = 0;
      virtual void send_framebuffer_update_request(bool incremental) = 0;
      virtual void handle_server_identity(std::string_view text) = 0;
      virtual void handle_supported_messages(const proto::rfbSupportedMessages& messages) = 0;
      virtual void handle_ext_desktop_screen(const std::vector<proto::rfbExtDesktopScreen>& screens) = 0;
      virtual void handle_resize_client_buffer(int w, int h) = 0;
};

class codec {
   public:
      virtual ~codec() = default;
      virtual void init() = 0;
      virtual proto::rfbEncoding encoding_code() const = 0;
      virtual std::string codec_name() const = 0;

      virtual bool is_frame_codec() const { return false; }

      virtual boost::asio::awaitable<error> decode(vnc_stream_type& socket,
                                                   const proto::rfbRectangle& rect,
                                                   frame_buffer& buffer,
                                                   std::shared_ptr<client_op> op) = 0;
};

class frame_codec : public codec {
   public:
      bool is_frame_codec() const final { return true; }

      boost::asio::awaitable<error> decode(vnc_stream_type& socket,
                                           const proto::rfbRectangle& rect,
                                           frame_buffer& buffer,
                                           std::shared_ptr<client_op> op) override;
};

class codec_manager {
   public:
      using codec_ptr = std::unique_ptr<codec>;

      codec_manager();

   public:
      boost::asio::awaitable<error> invoke(proto::rfbEncoding code,
                                           vnc_stream_type& socket,
                                           const proto::rfbRectangle& rect,
                                           frame_buffer& frame,
                                           std::shared_ptr<client_op> op);

   public:
      void init();
      void register_codec(codec_ptr&& ptr);
      bool has_codec(proto::rfbEncoding code) const;
      std::vector<proto::rfbEncoding> registered_encodings() const;
      std::vector<std::string> supported_frame_encodings() const;

      std::vector<proto::rfbEncoding> get_apply_encodings(const std::vector<std::string>& frame_encodings) const;

   private:
      template <typename T, typename... _Types>
         requires std::derived_from<T, encoding::codec>
      void register_encoding(_Types&&... _Args) {
         auto codec = std::make_unique<T>(std::forward<_Types>(_Args)...);
         codecs_.push_back(std::move(codec));
      }

   private:
      std::vector<codec_ptr> codecs_;
};

}  // namespace libvnc::encoding
