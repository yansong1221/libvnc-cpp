
#include "libvnc-cpp/client.h"
#include <boost/asio/io_context.hpp>
#include <filesystem>
#include <format>
#include <iostream>
int main()
{
    boost::asio::io_context ioc;
    libvnc::client cli(ioc, "100.64.0.15");
    cli.start();
    return ioc.run();
}