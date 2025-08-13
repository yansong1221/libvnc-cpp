#pragma once
#include "encoding.h"
#include "use_awaitable.hpp"
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>

namespace libvnc::encoding {

class ext_desktop_size : public codec
{
public:
    std::string codec_name() const override { return "ext-desktop-size"; }

    void reset() override { }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingExtDesktopSize;
    }

    boost::asio::awaitable<error> decode(boost::asio::ip::tcp::socket& socket,
                                         const proto::rfbRectangle& rect,
                                         frame_buffer& buffer,
                                         std::shared_ptr<frame_op> op) override
    {
        boost::system::error_code ec;

        proto::rfbExtDesktopSizeMsg eds {};
        co_await boost::asio::async_read(
            socket, boost::asio::buffer(&eds, sizeof(eds)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        std::vector<proto::rfbExtDesktopScreen> screens;

        proto::rfbExtDesktopScreen screen {};
        auto screen_num = eds.numberOfScreens.value();
        for (int loop = 0; loop < screen_num; loop++) {
            co_await boost::asio::async_read(
                socket, boost::asio::buffer(&screen, sizeof(screen)), net_awaitable[ec]);
            if (ec)
                co_return error::make_error(ec);

            screens.push_back(screen);
        }
        op->handle_ext_desktop_screen(screens);
        co_return error {};
    }
};
} // namespace libvnc::encoding