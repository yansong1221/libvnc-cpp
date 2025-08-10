#include "libvnc-cpp/error.h"
#include <spdlog/spdlog.h>

namespace libvnc {
error::error(const boost::system::error_code& system_err)
    : error_(system_err)
{
}

error::error(const custom_error& custom_err)
    : error_(custom_err)
{
}

int error::value() const noexcept
{
    return std::visit([](const auto& v) { return v.value(); }, error_);
}

std::string error::message() const noexcept
{
    return std::visit([](const auto& v) { return v.message(); }, error_);
}

bool error::has_error() const noexcept
{
    return std::visit([](const auto& v) { return static_cast<bool>(v); }, error_);
}

bool error::is_system_error() const noexcept
{
    return std::holds_alternative<boost::system::error_code>(error_);
}

error error::make_error(custom_error::code c, std::string msg)
{
    spdlog::error(msg);
    return custom_error(c, msg);
}

error error::make_error(const boost::system::error_code& ec)
{
    return ec;
}


} // namespace libvnc