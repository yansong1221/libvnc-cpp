#include "libvnc-cpp/client.h"
#include "client_impl.h"
#include <iostream>
#include <ranges>

namespace libvnc {


std::string client_delegate::get_auth_password() const
{
    std::string password;
    std::cout << "Enter password: ";
    std::getline(std::cin, password);
    return password;
}

proto::rfbPixelFormat client_delegate::want_format() const
{
    libvnc::proto::rfbPixelFormat format(8, 3, 4);
    format.redShift   = 16;
    format.greenShift = 8;
    format.blueShift  = 0;
    return format;
}

void client_delegate::on_bell()
{
    return;
}

libvnc::proto::rfbAuthScheme
client_delegate::select_auth_scheme(const std::set<proto::rfbAuthScheme>& auths) const
{
    if (auths.empty())
        return proto::rfbConnFailed;

    if (auths.count(proto::rfbNoAuth))
        return proto::rfbNoAuth;

    if (auths.count(proto::rfbVncAuth))
        return proto::rfbVncAuth;

    return *auths.begin();
}


void client_delegate::on_text_chat(const proto::rfbTextChatType& type, std::string_view message)
{
    return;
}

client::client(boost::asio::io_context& executor, client_delegate* handler)
    : impl_(std::make_shared<client_impl>(executor.get_executor(), handler))
{
}

client::~client()
{
    impl_->close();
}

void client::start()
{
    impl_->start();
}

void client::close()
{
    impl_->close();
}

void client::set_delegate(client_delegate* handler)
{

}

void client::set_host(std::string_view host)
{
    impl_->host_ = host;
}

void client::set_port(int port)
{
    impl_->port_ = port;
}

void client::set_share_desktop(bool share)
{
    impl_->share_desktop_ = share;
}

void client::set_compress_level(int level)
{
    impl_->compress_level_ = std::clamp(level, 0, 9);
}

void client::set_quality_level(int level)
{
    impl_->quality_level_ = std::clamp(level, 0, 9);
}

const frame_buffer& client::frame() const
{
    return impl_->frame();
}


void client::set_format(const proto::rfbPixelFormat& format)
{
    impl_->set_format(format);
}

void client::send_pointer_event(int x, int y, int buttonMask)
{
    impl_->send_pointer_event(x, y, buttonMask);
}

void client::send_key_event(uint32_t key, bool down)
{
    impl_->send_key_event(key, down);
}


int client::current_keyboard_led_state() const
{
    return impl_->current_keyboard_led_state();
}

} // namespace libvnc
