#pragma once
#include "libvnc-cpp/client.h"
#include <shared_mutex>

namespace libvnc {
class client_delegate_proxy {
   public:
      void reset(client_delegate* handler) {
         std::unique_lock<std::shared_mutex> lck(mutex_);
         handler_ = handler;
      }

      auto on_connect(const error& ec) { return invoke(&client_delegate::on_connect, ec); }

      auto on_disconnect(const error& ec) { return invoke(&client_delegate::on_disconnect, ec); }

      auto query_auth_scheme(const std::set<client_delegate::auth_mothed>& auths) {
         return invoke(&client_delegate::query_auth_scheme, auths);
      }

      auto get_auth_password() { return invoke(&client_delegate::get_auth_password); }

      auto get_auth_ms_account() { return invoke(&client_delegate::get_auth_ms_account); }

      auto want_format() { return invoke(&client_delegate::want_format); }

      auto on_new_frame_size(int w, int h) { return invoke(&client_delegate::on_new_frame_size, w, h); }

      auto on_keyboard_led_state(int state) { return invoke(&client_delegate::on_keyboard_led_state, state); }

      auto on_frame_update(const frame_buffer& frame) { return invoke(&client_delegate::on_frame_update, frame); }

      auto on_bell() { return invoke(&client_delegate::on_bell); }

      auto on_cut_text(std::string_view text) { return invoke(&client_delegate::on_cut_text, text); }

      auto on_cut_text_utf8(std::string_view text) { return invoke(&client_delegate::on_cut_text_utf8, text); }

      auto on_text_chat(const proto::rfbTextChatType& type, std::string_view message) {
         return invoke(&client_delegate::on_text_chat, type, message);
      }

      auto on_cursor_shape(int xhot, int yhot, const frame_buffer& rc_source, std::span<const uint8_t> rc_mask) {
         return invoke(&client_delegate::on_cursor_shape, xhot, yhot, rc_source, rc_mask);
      }

      auto on_cursor_pos(int x, int y) { return invoke(&client_delegate::on_cursor_pos, x, y); }

      auto on_status_changed(const client::status& s) { return invoke(&client_delegate::on_status_changed, s); }

      auto on_monitor_info(int count) { return invoke(&client_delegate::on_monitor_info, count); }

      template <typename Func, typename... Args>
      auto invoke(Func func, Args&&... args) {
         using result_t = std::invoke_result_t<Func, client_delegate*, Args...>;
         std::shared_lock lck(mutex_);
         if(!handler_) {
            if constexpr(!std::is_void_v<result_t>)
               return std::optional<result_t>{};
            else
               return;
         }
         if constexpr(!std::is_void_v<result_t>)
            return std::optional<result_t>{std::invoke(func, handler_, std::forward<Args>(args)...)};
         else
            std::invoke(func, handler_, std::forward<Args>(args)...);
      }

   private:
      client_delegate* handler_ = nullptr;
      std::shared_mutex mutex_;
};
}  // namespace libvnc