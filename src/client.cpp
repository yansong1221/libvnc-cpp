#include "libvnc-cpp/client.h"
#include "client_impl.h"
#include <ranges>

namespace libvnc {

client::client(boost::asio::io_context& executor,
               const proto::rfbPixelFormat& format,
               std::string_view host,
               uint16_t port /*= 5900*/)
    : impl_(std::make_shared<client_impl>(executor, format, host, port))
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

int client::get_width() const
{
    return impl_->get_width();
}

int client::get_height() const
{
    return impl_->get_height();
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

void client::set_connect_handler(connect_handler_type&& handler)
{
    impl_->set_connect_handler(std::move(handler));
}

void client::set_password_handler(password_handler_type&& handler)
{
    impl_->set_password_handler(std::move(handler));
}

void client::set_disconnect_handler(disconnect_handler_type&& handler)
{
    impl_->set_disconnect_handler(std::move(handler));
}

void client::set_bell_handler(bell_handler_type&& handler)
{
    impl_->set_bell_handler(std::move(handler));
}

void client::set_select_auth_scheme_handler(select_auth_scheme_handler_type&& handler)
{
    impl_->set_select_auth_scheme_handler(std::move(handler));
}

} // namespace libvnc
