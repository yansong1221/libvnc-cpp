#pragma once
#include "error.h"
#include "frame_buffer.h"
#include "proto.h"
#include <boost/asio/any_io_executor.hpp>
#include <memory>
#include <set>

namespace libvnc {

class client_delegate
{
public:
    virtual ~client_delegate()                        = default;
    virtual void on_connect(const error& ec)          = 0;
    virtual void on_disconnect(const error& ec)       = 0;
    virtual void on_frame_update(const frame_buffer&) = 0;
    virtual void on_keyboard_led_state(int state);
    virtual void on_text_chat(const proto::rfbTextChatType& type, std::string_view message);
    virtual void on_cut_text_utf8(std::string_view message);
    virtual void on_cut_text(std::string_view message);
    virtual void on_bell();

    virtual std::string get_auth_password() const;
    virtual proto::rfbPixelFormat want_format() const;
    virtual proto::rfbAuthScheme
    select_auth_scheme(const std::set<proto::rfbAuthScheme>& auths) const;
};

class client_impl;
class client
{
public:
    client(boost::asio::io_context& executor, client_delegate* handler);
    virtual ~client();

public:
    void start();
    void stop();

    void set_host(std::string_view host);
    void set_port(int port);
    void set_share_desktop(bool share);
    void set_compress_level(int level);
    void set_quality_level(int level);

    const frame_buffer& frame() const;

    bool send_format(const proto::rfbPixelFormat& format);
    bool send_frame_encodings(const std::vector<std::string>& encodings);
    bool send_scale_setting(int scale);
    bool send_ext_desktop_size(const std::vector<proto::rfbExtDesktopScreen>& screens);

    bool send_pointer_event(int x, int y, int buttonMask);
    bool send_key_event(uint32_t key, bool down);
    bool send_extended_key_event(uint32_t keysym, uint32_t keycode, bool down);
    bool send_client_cut_text(std::string_view text);
    bool send_client_cut_text_utf8(std::string_view text);

    bool text_chat_send(std::string_view text);
    bool text_chat_open();
    bool text_chat_close();
    bool text_chat_finish();

    bool permit_server_input(bool enabled);

    bool send_xvp_msg(uint8_t version, proto::rfbXvpCode code);

    int current_keyboard_led_state() const;

private:
    client(const client&)            = delete;
    client& operator=(const client&) = delete;

    friend class client_impl;
    std::shared_ptr<client_impl> impl_;
};
} // namespace libvnc