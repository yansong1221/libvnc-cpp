#include "libvnc-cpp/client.h"
#include "client_impl.h"
#include <iostream>
#include <ranges>

namespace libvnc {

client::client(boost::asio::io_context& executor, client_delegate* handler)
    : impl_(std::make_shared<client_impl>(executor.get_executor()))
{
    impl_->handler_.reset(handler);
}

client::~client()
{
    impl_->handler_.reset(nullptr);
    impl_->close();
}

void client::start()
{
    impl_->start();
}

void client::stop()
{
    impl_->close();
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

void client::set_notifiction_text(std::string_view text)
{
    impl_->notifiction_text_ = text;
}

const frame_buffer& client::frame() const
{
    return impl_->frame();
}

client::status client::current_status() const
{
    return impl_->status_;
}

bool client::send_frame_encodings(const std::vector<std::string>& encodings)
{
    return impl_->send_frame_encodings(encodings);
}

bool client::send_scale_setting(int scale)
{
    return impl_->send_scale_setting(scale);
}

bool client::send_ext_desktop_size(const std::vector<proto::rfbExtDesktopScreen>& screens)
{
    return impl_->send_ext_desktop_size(screens);
}

bool client::send_pointer_event(int x, int y, int buttonMask)
{
    return impl_->send_pointer_event(x, y, buttonMask);
}

bool client::send_key_event(uint32_t key, bool down)
{
    return impl_->send_key_event(key, down);
}

bool client::send_extended_key_event(uint32_t keysym, uint32_t keycode, bool down)
{
    return impl_->send_extended_key_event(keysym, keycode, down);
}

bool client::send_client_cut_text(std::string_view text)
{
    return impl_->send_client_cut_text(text);
}

bool client::send_client_cut_text_utf8(std::string_view text)
{
    return impl_->send_client_cut_text_utf8(text);
}

bool client::text_chat_send(std::string_view text)
{
    return impl_->text_chat_send(text);
}

bool client::text_chat_open()
{
    return impl_->text_chat_open();
}

bool client::text_chat_close()
{
    return impl_->text_chat_close();
}

bool client::text_chat_finish()
{
    return impl_->text_chat_finish();
}

bool client::permit_server_input(bool enabled)
{
    return impl_->permit_server_input(enabled);
}

bool client::send_xvp_msg(uint8_t version, proto::rfbXvpCode code)
{
    return impl_->send_xvp_msg(version, code);
}

bool client::send_set_monitor(uint8_t nbr)
{
    return impl_->send_set_monitor(nbr);
}

int client::current_keyboard_led_state() const
{
    return impl_->current_keyboard_led_state_;
}

int client::monitors() const
{
    return impl_->nbrMonitors_;
}

std::string client_delegate::get_auth_password()
{
    std::string password;
    std::cout << "Enter password: ";
    std::getline(std::cin, password);
    return password;
}

std::pair<std::string, std::string> client_delegate::get_auth_ms_account()
{
    std::pair<std::string, std::string> account;

    std::cout << "Enter ms user: ";
    std::getline(std::cin, account.first);
    std::cout << "Enter ms password: ";
    std::getline(std::cin, account.second);

    return account;
}

proto::rfbPixelFormat client_delegate::want_format()
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

void client_delegate::on_cursor_shape(int xhot,
                                      int yhot,
                                      const frame_buffer& rc_source,
                                      const uint8_t* rc_mask)
{
}

void client_delegate::on_cursor_pos(int x, int y)
{
}

void client_delegate::on_status_changed(const client::status& s)
{
}

void client_delegate::on_monitor_info(int count)
{
}

libvnc::proto::rfbAuthScheme
client_delegate::select_auth_scheme(const std::set<proto::rfbAuthScheme>& auths)
{
    if (auths.empty())
        return proto::rfbConnFailed;

    if (auths.count(proto::rfbNoAuth))
        return proto::rfbNoAuth;

    if (auths.count(proto::rfbUltraMSLogonII))
        return proto::rfbUltraMSLogonII;

    if (auths.count(proto::rfbVncAuth))
        return proto::rfbVncAuth;

    return *auths.begin();
}


void client_delegate::on_keyboard_led_state(int state)
{
}

void client_delegate::on_text_chat(const proto::rfbTextChatType& type, std::string_view message)
{
    return;
}

void client_delegate::on_cut_text_utf8(std::string_view message)
{
}

void client_delegate::on_cut_text(std::string_view message)
{
}
} // namespace libvnc
