#pragma once
#include "error.h"
#include "frame_buffer.h"
#include "proto.h"
#include <boost/asio/any_io_executor.hpp>
#include <memory>
#include <set>

namespace libvnc {
class error;
}

namespace libvnc {


class client_delegate
{
public:
    virtual ~client_delegate()                  = default;
    virtual void on_connect(const error& ec)    = 0;
    virtual void on_disconnect(const error& ec) = 0;
    virtual std::string get_auth_password() const;
    virtual proto::rfbPixelFormat want_format() const;
    virtual void on_bell();
    virtual proto::rfbAuthScheme
    select_auth_scheme(const std::set<proto::rfbAuthScheme>& auths) const;
    virtual void on_text_chat(const proto::rfbTextChatType& type, std::string_view message);
    virtual void on_frame_update(const frame_buffer&) = 0;
    virtual void on_keyboard_led_state(int state) { }
};

class client_impl;
class client
{
public:
    client(boost::asio::io_context& executor, client_delegate* handler);
    virtual ~client();

public:
    void start();
    void close();

    void set_delegate(client_delegate* handler);

    void set_host(std::string_view host);
    void set_port(int port);
    void set_share_desktop(bool share);
    void set_compress_level(int level);
    void set_quality_level(int level);

    const frame_buffer& frame() const;

    void set_format(const proto::rfbPixelFormat& format);
    void send_pointer_event(int x, int y, int buttonMask);
    void send_key_event(uint32_t key, bool down);

    int current_keyboard_led_state() const;

private:
    friend class client_impl;
    std::shared_ptr<client_impl> impl_;
};
} // namespace libvnc