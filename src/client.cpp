#include "libvnc-cpp/client.h"
#include "client_impl.h"
#include <ranges>

namespace libvnc {

client::client(boost::asio::io_context& executor, std::string_view host, uint16_t port)
    : impl_(std::make_unique<client_impl>(this, executor, host, port))
{
}

client::~client()
{
}

void client::start()
{
    impl_->start();
}

libvnc::client::auth_scheme_type
client::select_auth_scheme(const std::vector<auth_scheme_type>& auths)
{
    if (auths.empty())
        return auth_scheme_type::rfbConnFailed;

    auto iter = std::ranges::find_if(
        auths, [](const auto& auth_scheme) { return auth_scheme == auth_scheme_type::rfbNoAuth; });

    if (iter != auths.end())
        return auth_scheme_type::rfbNoAuth;

    return auths.front();
}

} // namespace libvnc
