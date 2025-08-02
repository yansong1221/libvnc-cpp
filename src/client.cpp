#include "libvnc-cpp/client.h"
#include "client_impl.h"

namespace libvnc {

client::client(const boost::asio::any_io_executor& executor)
    : impl_(std::make_unique<client_impl>(executor))
{
}

client::~client()
{
}
} // namespace libvnc
