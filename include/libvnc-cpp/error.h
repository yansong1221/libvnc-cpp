#pragma once
#include <boost/system/error_code.hpp>
#include <format>

#include <string>
#include <utility>
#include <variant>

namespace libvnc {

class custom_error
{
public:
    enum code : int
    {
        none              = 0,
        auth_error        = 1,
        client_init_error = 2
    };

    custom_error() = default;
    custom_error(code c, std::string msg)
        : code_(c)
        , message_(std::move(msg))
    {
    }

    bool has_error() const noexcept { return code_ != none; }
    explicit operator bool() const noexcept { return has_error(); }

    int value() const noexcept { return static_cast<int>(code_); }
    const std::string& message() const noexcept { return message_; }


private:
    code code_ = none;
    std::string message_;
};

class error
{
public:
    error() = default;
    error(const boost::system::error_code& system_err);
    error(const custom_error& custom_err);

    int value() const noexcept;

    std::string message() const noexcept;
    bool has_error() const noexcept;

    explicit operator bool() const noexcept { return has_error(); }

    bool is_system_error() const noexcept;

public:
    static error make_error(custom_error::code c, std::string msg);
    static error make_error(const boost::system::error_code& ec);

private:
    std::variant<boost::system::error_code, custom_error> error_;
};

} // namespace libvnc