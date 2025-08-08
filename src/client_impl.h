#pragma once
#include "encoding/encoding.h"
#include "libvnc-cpp/client.h"
#include "libvnc-cpp/proto.h"
#include "rfb.h"
#include "spdlog/spdlog.h"
#include "supported_messages.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/strand.hpp>
#include <span>

namespace libvnc {

class client_impl : public encoding::frame_op
{
public:
    client_impl(boost::asio::io_context& executor,
                const proto::rfbPixelFormat& format,
                std::string_view host,
                uint16_t port);

public:
    int get_width() const;
    int get_height() const;

    void close();
    void start();

    void set_format(const proto::rfbPixelFormat& format);
    void set_encodings(const std::vector<std::string>& encodings);

    void send_framebuffer_update_request(int x, int y, int w, int h, bool incremental);
    std::vector<std::string> supported_encodings() const;

    void send_pointer_event(int x, int y, int buttonMask);
    void send_key_event(uint32_t key, bool down);

    boost::asio::awaitable<boost::system::error_code> async_connect_rfbserver();

public:
    void set_connect_handler(client::connect_handler_type&& handler)
    {
        boost::asio::dispatch(executor_,
                              [this, self = shared_from_this(), handler = std::move(handler)]() {
                                  connect_handler_ = std::move(handler);
                              });
    }
    void set_password_handler(client::password_handler_type&& handler)
    {
        boost::asio::dispatch(executor_,
                              [this, self = shared_from_this(), handler = std::move(handler)]() {
                                  password_handler_ = std::move(handler);
                              });
    }
    void set_disconnect_handler(client::disconnect_handler_type&& handler)
    {
        boost::asio::dispatch(executor_,
                              [this, self = shared_from_this(), handler = std::move(handler)]() {
                                  disconnect_handler_ = std::move(handler);
                              });
    }
    void set_bell_handler(client::bell_handler_type&& handler)
    {
        boost::asio::dispatch(executor_,
                              [this, self = shared_from_this(), handler = std::move(handler)]() {
                                  bell_handler_ = std::move(handler);
                              });
    }
    void set_select_auth_scheme_handler(client::select_auth_scheme_handler_type&& handler)
    {
        boost::asio::dispatch(executor_,
                              [this, self = shared_from_this(), handler = std::move(handler)]() {
                                  select_auth_scheme_handler_ = std::move(handler);
                              });
    }
    void set_text_chat_handler(client::text_chat_handler_type&& handler)
    {
        boost::asio::dispatch(executor_,
                              [this, self = shared_from_this(), handler = std::move(handler)]() {
                                  text_chat_handler_ = std::move(handler);
                              });
    }

private:
    boost::asio::awaitable<void> async_authenticate();
    boost::asio::awaitable<void> async_client_init();

    boost::asio::awaitable<void> server_message_loop();

    boost::asio::awaitable<void> read_auth_result();
    boost::asio::awaitable<std::string> read_error_reason();

    void send_msg_to_server(const proto::rfbClientToServerMsg& ID,
                            const void* data,
                            std::size_t len);
    void send_raw_data(const std::span<uint8_t>& data);
    void send_raw_data(std::vector<uint8_t>&& data);

    void malloc_frame_buffer();
    bool check_rect(int x, int y, int w, int h) const;

    std::string inner_get_password() const;
    proto::rfbAuthScheme inner_select_auth_scheme(const std::vector<proto::rfbAuthScheme>& auths);

    template<typename T>
    void copy_rect_from_rect(int src_x, int src_y, int w, int h, int dest_x, int dest_y);

    encoding::codec* find_encoding(const proto::rfbEncoding& encoding);

protected:
    void got_bitmap(const uint8_t* buffer, int x, int y, int w, int h) override;
    void soft_cursor_lock_area(int x, int y, int w, int h) override { }
    void got_copy_rect(int src_x, int src_y, int w, int h, int dest_x, int dest_y) override;
    void got_fill_rect(int x, int y, int w, int h, uint32_t colour) override;
    void resize_client_buffer(int width, int height) override;

private:
    boost::asio::awaitable<void> on_message(const proto::rfbFramebufferUpdateMsg& msg);
    boost::asio::awaitable<void> on_message(const proto::rfbTextChatMsg& msg);
    boost::asio::awaitable<void> on_message(const proto::rfbXvpMsg& msg);
    boost::asio::awaitable<void> on_message(const proto::rfbResizeFrameBufferMsg& msg);
    boost::asio::awaitable<void> on_message(const proto::rfbPalmVNCReSizeFrameBufferMsg& msg);
    boost::asio::awaitable<void> on_message(const proto::rfbServerCutTextMsg& msg);


private:
    boost::asio::io_context& executor_;

    std::list<std::vector<uint8_t>> send_que_;
    std::vector<uint8_t> frame_buffer_;

    std::string host_ = "127.0.0.1";
    uint16_t port_    = 5900;

    proto::rfbPixelFormat want_format_;
    proto::rfbPixelFormat format_;

    int width_  = 0;
    int height_ = 0;
    proto::rfbPixelFormat server_format_;
    std::string desktop_name_;

    /** negotiated protocol version */
    int major_ = proto::rfbProtocolMajorVersion, minor_ = proto::rfbProtocolMinorVersion;

    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;

    client::password_handler_type password_handler_;
    client::connect_handler_type connect_handler_;
    client::disconnect_handler_type disconnect_handler_;
    client::bell_handler_type bell_handler_;
    client::select_auth_scheme_handler_type select_auth_scheme_handler_;
    client::text_chat_handler_type text_chat_handler_;


    supported_messages supported_messages_;

    struct AppData
    {
        bool shareDesktop;
        bool viewOnly;

        const char* encodingsString;

        bool useBGR233;
        int nColours;
        bool forceOwnCmap;
        bool forceTrueColour;
        int requestedDepth;

        int compressLevel;
        int qualityLevel;
        bool enableJPEG;
        bool useRemoteCursor;
        bool palmVNC;     /**< use palmvnc specific SetScale (vs ultravnc) */
        int scaleSetting; /**< 0 means no scale set, else 1/scaleSetting */
    };
    AppData app_data_;

    proto::rfbExtDesktopScreen screen_;

    std::vector<std::unique_ptr<encoding::frame_codec>> frame_codecs_;
    std::vector<std::unique_ptr<encoding::codec>> codecs_;
};
} // namespace libvnc

#include "client_impl.inl"