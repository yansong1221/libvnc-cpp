#pragma once
#include "encoding.h"
#include <boost/asio/read.hpp>

namespace libvnc::encoding {

class keyboard_led_state : public codec
{
public:
    void reset() override { }
    proto::rfbEncoding encoding_code() const override
    {
        return proto::rfbEncoding::rfbEncodingKeyboardLedState;
    }

    boost::asio::awaitable<bool> decode(boost::asio::ip::tcp::socket& socket,
                                        const proto::rfbRectangle& rect,
                                        const proto::rfbPixelFormat& format,
                                        std::shared_ptr<frame_op> op) override
    {
        ///* OK! We have received a keyboard state message!!! */
        //client->KeyboardLedStateEnabled = 1;
        //if (client->HandleKeyboardLedState != NULL)
        //    client->HandleKeyboardLedState(client, rect.r.x, 0);
        ///* stash it for the future */
        //client->CurrentKeyboardLedState = rect.r.x;
        co_return true;
    }
};
} // namespace libvnc::encoding