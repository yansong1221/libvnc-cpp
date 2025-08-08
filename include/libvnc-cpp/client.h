#pragma once
#include "proto.h"
#include <boost/asio/any_io_executor.hpp>
#include <memory>

namespace libvnc {
class client_impl;
class client
{
public:
    using connect_handler_type    = std::function<void(const boost::system::error_code&)>;
    using password_handler_type   = std::function<std::string()>;
    using disconnect_handler_type = std::function<void(const boost::system::error_code&)>;
    using bell_handler_type       = std::function<void()>;
    using select_auth_scheme_handler_type =
        std::function<proto::rfbAuthScheme(const std::vector<proto::rfbAuthScheme>&)>;

public:
    client(boost::asio::io_context& executor,
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

public:
    void set_connect_handler(connect_handler_type&& handler);
    void set_password_handler(password_handler_type&& handler);
    void set_disconnect_handler(disconnect_handler_type&& handler);
    void set_bell_handler(bell_handler_type&& handler);
    void set_select_auth_scheme_handler(select_auth_scheme_handler_type&& handler);

private:
    friend class client_impl;
    std::shared_ptr<client_impl> impl_;
};
} // namespace libvnc