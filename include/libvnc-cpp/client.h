#pragma once
#include "proto.h"
#include <boost/asio/any_io_executor.hpp>
#include <memory>
#include <set>

namespace libvnc {


class client_delegate
{
public:
    virtual ~client_delegate()                                      = default;
    virtual void on_connect(const boost::system::error_code& ec)    = 0;
    virtual void on_disconnect(const boost::system::error_code& ec) = 0;
    virtual std::string get_auth_password() const;
    virtual void on_bell();
    virtual proto::rfbAuthScheme
    select_auth_scheme(const std::set<proto::rfbAuthScheme>& auths) const;
    virtual void on_text_chat(const proto::rfbTextChatType& type, std::string_view message);
    virtual void on_frame_update(const uint8_t* buffer) = 0;
};

class client_impl;
class client
{
public:
    client(boost::asio::io_context& executor,
           client_delegate* handler,
           const proto::rfbPixelFormat& format,
           std::string_view host,
           uint16_t port = 5900);
    virtual ~client();

public:
    void start();
    void close();

    int get_width() const;
    int get_height() const;

    void set_format(const proto::rfbPixelFormat& format);
    void send_pointer_event(int x, int y, int buttonMask);
    void send_key_event(uint32_t key, bool down);

private:
    friend class client_impl;
    std::shared_ptr<client_impl> impl_;
};
} // namespace libvnc