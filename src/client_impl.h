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
#include <set>
#include <span>

namespace libvnc {

class client_impl : public encoding::frame_op
{
public:
    client_impl(const boost::asio::any_io_executor& executor,
                client_delegate* handler,
                std::string_view host,
                uint16_t port);

public:
    const frame_buffer& frame() const;


    void close();
    void start();

    void set_format(const proto::rfbPixelFormat& format);
    void set_encodings(const std::vector<std::string>& encodings);

    void send_framebuffer_update_request(int x, int y, int w, int h, bool incremental);
    void send_framebuffer_update_request(bool incremental);

    std::vector<std::string> supported_encodings() const;

    void send_pointer_event(int x, int y, int buttonMask);
    void send_key_event(uint32_t key, bool down);

    boost::asio::awaitable<error> async_connect_rfbserver();

private:
    boost::asio::awaitable<error> async_authenticate();
    boost::asio::awaitable<error> async_client_init();

    boost::asio::awaitable<void> server_message_loop();

    boost::asio::awaitable<error> read_auth_result();
    boost::asio::awaitable<error> read_error_reason();

    bool send_msg_to_server(const proto::rfbClientToServerMsg& ID,
                            const void* data,
                            std::size_t len);
    void send_raw_data(const std::span<uint8_t>& data);
    void send_raw_data(std::vector<uint8_t>&& data);


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
    boost::asio::strand<boost::asio::any_io_executor> strand_;

    std::list<std::vector<uint8_t>> send_que_;

    std::string host_ = "127.0.0.1";
    uint16_t port_    = 5900;

    frame_buffer buffer_;
    std::mutex buffer_mutex_;

    std::string desktop_name_;

    /** negotiated protocol version */
    int major_ = proto::rfbProtocolMajorVersion, minor_ = proto::rfbProtocolMinorVersion;

    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;

    client_delegate* handler_;


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