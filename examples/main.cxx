
#include "libvnc-cpp/client.h"
#include <boost/asio/io_context.hpp>
#include <filesystem>
#include <format>
#include <iostream>
int main()
{
    boost::asio::io_context ioc;

    libvnc::proto::rfbPixelFormat format(8, 3, 4);
    libvnc::client cli(ioc, format, "127.0.0.1");
    cli.start();
    return ioc.run();
}