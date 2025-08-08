#pragma once
#include "libvnc-cpp/proto.h"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace libvnc::encoding {

class frame_op : public std::enable_shared_from_this<frame_op>
{
public:
    virtual ~frame_op()                                                        = default;
    virtual void got_bitmap(const uint8_t* buffer, int x, int y, int w, int h) = 0;
    virtual void soft_cursor_lock_area(int x, int y, int w, int h) { }
    virtual void got_copy_rect(int src_x, int src_y, int w, int h, int dest_x, int dest_y) = 0;
    virtual void got_fill_rect(int x, int y, int w, int h, uint32_t colour)                = 0;
    virtual void got_cursor_shape(int xhot, int yhot, int width, int height, int bytesPerPixel) { }
    virtual void handle_cursor_pos(int x, int y) { }
    virtual void resize_client_buffer(int width, int height) = 0;
};

class codec
{
public:
    virtual ~codec()                                                          = default;
    virtual void reset()                                                      = 0;
    virtual proto::rfbEncoding encoding_code() const                          = 0;
    virtual boost::asio::awaitable<bool> decode(boost::asio::ip::tcp::socket& socket,
                                                const proto::rfbRectangle& rect,
                                                const proto::rfbPixelFormat& format,
                                                std::shared_ptr<frame_op> op) = 0;
};

class frame_codec : public codec
{
public:
    virtual std::string codec_name() const = 0;
    virtual bool requestLastRectEncoding() const { return false; }
    virtual bool requestCompressLevel() const { return false; }
    virtual bool requestQualityLevel() const { return false; }
};

} // namespace libvnc::encoding
