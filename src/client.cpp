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

client::client(boost::asio::io_context& executor,
               client_delegate* handler,
               const proto::rfbPixelFormat& format,
               std::string_view host,
               uint16_t port /*= 5900*/)
    : impl_(std::make_shared<client_impl>(executor, handler, format, host, port))
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


} // namespace libvnc
