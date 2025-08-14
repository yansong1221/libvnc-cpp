#pragma once
#include "client_delegate_wrapper.hpp"
#include "encoding/encoding.h"
#include "libvnc-cpp/client.h"
#include "libvnc-cpp/error.h"
#include "libvnc-cpp/proto.h"
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

    bool send_format(const proto::rfbPixelFormat& format);
    bool send_frame_encodings(const std::vector<std::string>& encodings);
    bool send_scale_setting(int scale);
    bool send_ext_desktop_size(const std::vector<proto::rfbExtDesktopScreen>& screens);
    bool send_key_event(uint32_t key, bool down);
    bool send_extended_key_event(uint32_t keysym, uint32_t keycode, bool down);
    bool send_pointer_event(int x, int y, int buttonMask);
    bool send_client_cut_text(std::string_view text);
    bool send_client_cut_text_utf8(std::string_view text);

    bool text_chat_send(std::string_view text);
    bool text_chat_open();
    bool text_chat_close();
    bool text_chat_finish();

    bool permit_server_input(bool enabled);

    bool send_xvp_msg(uint8_t version, proto::rfbXvpCode code);

    void send_framebuffer_update_request(int x, int y, int w, int h, bool incremental);
    void send_framebuffer_update_request(bool incremental) override;

    std::vector<std::string> supported_frame_encodings() const;


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
    bool send_msg_to_server_buffers(const proto::rfbClientToServerMsg& ID,
                                    const std::vector<boost::asio::const_buffer>& buffers);

    template<typename... Buffers>
    bool send_msg_to_server_buffers(const proto::rfbClientToServerMsg& ID,
                                    const Buffers&... buffers)
    {
        std::vector<boost::asio::const_buffer> bufs;
        (bufs.push_back(boost::asio::buffer(buffers)), ...);
        return send_msg_to_server_buffers(ID, bufs);
    }
    void send_raw_data(std::vector<uint8_t>&& data);

protected:
    void soft_cursor_lock_area(int x, int y, int w, int h) override;
    void got_cursor_shape(int xhot,
                          int yhot,
                          const frame_buffer& rc_source,
                          const uint8_t* rc_mask) override;
    void handle_cursor_pos(int x, int y) override { }
    void handle_keyboard_led_state(int state) override;
    void handle_server_identity(std::string_view text) override;
    void handle_supported_messages(const proto::rfbSupportedMessages& messages) override;
    void handle_ext_desktop_screen(const std::vector<proto::rfbExtDesktopScreen>& screens) override;
    void handle_resize_client_buffer(int width, int height);

private:
    boost::asio::awaitable<error> on_rfbFramebufferUpdate();
    boost::asio::awaitable<error> on_rfbSetColourMapEntries();
    boost::asio::awaitable<error> on_rfbBell();
    boost::asio::awaitable<error> on_rfbServerCutText();
    boost::asio::awaitable<error> on_rfbTextChat();
    boost::asio::awaitable<error> on_rfbXvp();
    boost::asio::awaitable<error> on_rfbResizeFrameBuffer();
    boost::asio::awaitable<error> on_rfbPalmVNCReSizeFrameBuffer();

    template<typename T, typename... _Types>
        requires std::derived_from<T, encoding::codec>
    void register_encoding(_Types&&... _Args)
    {
        auto codec = std::make_unique<T>(std::forward<_Types>(_Args)...);
        codecs_.push_back(std::move(codec));
    }


public:
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
    std::deque<std::vector<uint8_t>> send_que_;
    bool is_initialization_completed_ = false;

    std::string host_        = "127.0.0.1";
    uint16_t port_           = 5900;
    bool share_desktop_      = true;
    uint32_t compress_level_ = 3;
    uint32_t quality_level_  = 5;

    frame_buffer frame_;

    std::string desktop_name_;
    int current_keyboard_led_state_ = 0;

    std::vector<proto::rfbExtDesktopScreen> screens_;

    /** negotiated protocol version */
    int major_ = proto::rfbProtocolMajorVersion, minor_ = proto::rfbProtocolMinorVersion;

    client_delegate_wrapper handler_;
    supported_messages supported_messages_;

    std::vector<std::unique_ptr<encoding::codec>> codecs_;
    using message_handler = std::function<boost::asio::awaitable<error>()>;
    std::map<uint8_t, message_handler> message_map_;

    std::bitset<32> extendedClipboardServerCapabilities_;
};


} // namespace libvnc