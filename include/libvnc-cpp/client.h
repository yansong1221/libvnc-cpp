#pragma once
#include <boost/asio/any_io_executor.hpp>
#include <memory>

namespace libvnc {
class client_impl;
class client
{
public:
    client(const boost::asio::any_io_executor& executor);
    virtual ~client();

private:
    std::unique_ptr<client_impl> impl_;
};
} // namespace libvnc