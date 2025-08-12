#pragma once
#include "encoding/encoding.h"
#include "libvnc-cpp/client.h"
#include "libvnc-cpp/error.h"
#include "libvnc-cpp/proto.h"
#include "rfb.h"
#include "spdlog/spdlog.h"
#include "supported_messages.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/strand.hpp>
#include <map>
#include <queue>
#include <set>
#include <span>

namespace libvnc {

class client_impl : public encoding::frame_op
{
public:
    client_impl(const boost::asio::any_io_executor& executor);

public:
    const frame_buffer& frame() const;

    void close();
    void start();
    boost::asio::awaitable<error> co_start();

    void set_delegate(client_delegate* handler);

    int current_keyboard_led_state() const;

    void set_format(const proto::rfbPixelFormat& format);
    void set_frame_encodings(const std::vector<std::string>& encodings);

    void send_framebuffer_update_request(int x, int y, int w, int h, bool incremental);
    void send_framebuffer_update_request(bool incremental) override;

    std::vector<std::string> supported_frame_encodings() const;

    void send_pointer_event(int x, int y, int buttonMask);
    void send_key_event(uint32_t key, bool down);

private:
    boost::asio::awaitable<error> async_connect_rfbserver();
    boost::asio::awaitable<error> async_authenticate();
    boost::asio::awaitable<error> async_client_init();

    boost::asio::awaitable<error> server_message_loop();

    boost::asio::awaitable<error> read_auth_result();
    boost::asio::awaitable<error> read_error_reason();

    bool send_msg_to_server(const proto::rfbClientToServerMsg& ID,
                            const void* data,
                            std::size_t len);
    void send_raw_data(const std::span<uint8_t>& data);
    void send_raw_data(std::vector<uint8_t>&& data);

protected:
    void soft_cursor_lock_area(int x, int y, int w, int h) override { }
    void got_cursor_shape(int xhot,
                          int yhot,
                          const frame_buffer& rc_source,
                          const uint8_t* rc_mask) override
    {
    }
    void handle_cursor_pos(int x, int y) override { }
    void HandleKeyboardLedState(int state) override;
    void handle_server_identity(std::string_view text) override;
    void handle_supported_messages(const proto::rfbSupportedMessages& messages) override;

    void resize_client_buffer(int width, int height);

private:
    boost::asio::awaitable<error> on_rfbFramebufferUpdate();
    boost::asio::awaitable<error> on_rfbSetColourMapEntries();
    boost::asio::awaitable<error> on_rfbBell();
    boost::asio::awaitable<error> on_rfbServerCutText();
    boost::asio::awaitable<error> on_rfbTextChat();
    boost::asio::awaitable<error> on_rfbXvp();
    boost::asio::awaitable<error> on_rfbResizeFrameBuffer();
    boost::asio::awaitable<error> on_rfbPalmVNCReSizeFrameBuffer();


public:
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
    std::deque<std::vector<uint8_t>> send_que_;
    bool is_initialization_completed_ = false;

    std::string host_   = "127.0.0.1";
    uint16_t port_      = 5900;
    bool share_desktop_ = true;
    int compress_level_ = 3;
    int quality_level_  = 5;

    frame_buffer buffer_;
    std::mutex buffer_mutex_;

    std::string desktop_name_;
    int current_keyboard_led_state_ = 0;

    /** negotiated protocol version */
    int major_ = proto::rfbProtocolMajorVersion, minor_ = proto::rfbProtocolMinorVersion;

    client_delegate* handler_ = nullptr;
    supported_messages supported_messages_;

    std::vector<std::unique_ptr<encoding::codec>> codecs_;
    using message_handler = std::function<boost::asio::awaitable<error>()>;
    std::map<uint8_t, message_handler> message_map_;
};


} // namespace libvnc