#include "encoding.h"
#include "copy_rect.hpp"
#include "cursor.hpp"
#include "ext_desktop_size.hpp"
#include "hextile.hpp"
#include "keyboard_led_state.hpp"
#include "new_fb_size.hpp"
#include "pointer_pos.hpp"
#include "raw.hpp"
#include "rre.hpp"
#include "server_identity.hpp"
#include "supported_encodings.hpp"
#include "supported_messages.hpp"
#include "tight.hpp"
#include "ultra.hpp"
#include "zlib.hpp"
#include "zrle.hpp"
#include <ranges>
#include <spdlog/spdlog.h>

namespace libvnc::encoding {

boost::asio::awaitable<libvnc::error> frame_codec::decode(vnc_stream_type& socket,
                                                          const proto::rfbRectangle& rect,
                                                          frame_buffer& buffer,
                                                          std::shared_ptr<client_op> op) {
   if(!buffer.check_rect(rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value())) {
      auto msg = fmt::format(
         "Rect too large: {}x{} at ({}, {})", rect.w.value(), rect.h.value(), rect.x.value(), rect.y.value());
      spdlog::error(msg);
      co_return error::make_error(custom_error::frame_error, msg);
   }
   op->soft_cursor_lock_area(rect.x.value(), rect.y.value(), rect.w.value(), rect.h.value());
   co_return error{};
}

codec_manager::codec_manager() {
   register_encoding<tight>();
   register_encoding<ultra>();
   register_encoding<ultra_zip>();
   register_encoding<copy_rect>();
   register_encoding<zrle>();
   register_encoding<zlib>();
   register_encoding<co_rre>();
   register_encoding<rre>();
   register_encoding<hextile>();
   register_encoding<raw>();

   register_encoding<x_cursor>();
   register_encoding<rich_cursor>();
   register_encoding<keyboard_led_state>();
   register_encoding<new_fb_size>();
   register_encoding<pointer_pos>();
   register_encoding<server_identity>();
   register_encoding<supported_encodings>();
   register_encoding<ext_desktop_size>();
   register_encoding<supported_messages>();
}

void codec_manager::register_codec(codec_ptr&& ptr) {
   codecs_.push_back(std::move(ptr));
}

bool codec_manager::has_codec(proto::rfbEncoding code) const {
   auto iter = std::ranges::find_if(codecs_, [&](const auto& ptr) { return ptr->encoding_code() == code; });
   return iter != codecs_.end();
}

std::vector<proto::rfbEncoding> codec_manager::registered_encodings() const {
   std::vector<proto::rfbEncoding> result;
   for(const auto& code : codecs_)
      result.push_back(code->encoding_code());

   return result;
}

void codec_manager::init() {
   for(const auto& codec : codecs_)
      codec->init();
}

std::vector<std::string> codec_manager::supported_frame_encodings() const {
   std::vector<std::string> encs;
   for(const auto& item : codecs_) {
      if(!item->is_frame_codec())
         continue;
      encs.push_back(item->codec_name());
   }
   return encs;
}

boost::asio::awaitable<libvnc::error> codec_manager::invoke(proto::rfbEncoding code,
                                                               vnc_stream_type& socket,
                                                               const proto::rfbRectangle& rect,
                                                               frame_buffer& frame,
                                                               std::shared_ptr<client_op> op) {
   auto iter = std::ranges::find_if(codecs_, [&](const auto& codec) { return codec->encoding_code() == code; });
   if(iter == codecs_.end()) {
      co_return error::make_error(custom_error::frame_error, fmt::format("Unsupported encoding: {}", (int)code));
   }
   const auto& codec = (*iter);

   co_return co_await codec->decode(socket, rect, frame, op);
}

std::vector<proto::rfbEncoding> codec_manager::get_apply_encodings(
   const std::vector<std::string>& frame_encodings) const {
   auto apply_codecs =
      codecs_ | std::views::filter([&](const auto& enc) {
         if(!enc->is_frame_codec())
            return true;

         auto iter =
            std::ranges::find_if(frame_encodings, [&](const auto& enc_name) { return enc->codec_name() == enc_name; });
         return iter != frame_encodings.end();
      }) |
      std::views::transform([](const auto& enc) { return enc->encoding_code(); }) | std::ranges::to<std::vector>();
   return apply_codecs;
}

}  // namespace libvnc::encoding