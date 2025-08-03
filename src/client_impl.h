#pragma once
#include "libvnc-cpp/client.h"
#include "proto.h"
#include "rfb.h"
#include "spdlog/spdlog.h"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <span>

namespace libvnc {

class client;
class client_impl
{
public:
    client_impl(client* self,
                boost::asio::io_context& executor,
                std::string_view host,
                uint16_t port);

public:
    void close();
    void start();

    void sendFramebufferUpdateRequest(int x, int y, int w, int h, bool incremental);

private:
    void SetClient2Server(int messageType);

    boost::asio::awaitable<void> connect();
    boost::asio::awaitable<void> handshake();
    boost::asio::awaitable<void> authenticate();
    boost::asio::awaitable<void> client_init();
    boost::asio::awaitable<void> server_message_loop();

    boost::asio::awaitable<void> read_auth_result();
    boost::asio::awaitable<std::string> read_error_reason();

    void send_data(const proto::rfbClientToServerMsg& ID, const void* data, std::size_t len);
    void send_raw_data(const std::span<uint8_t>& data);
    void send_raw_data(std::vector<uint8_t>&& data);

private:
    boost::asio::awaitable<void> on_message(const proto::rfbFramebufferUpdateMsg& msg);


private:
    boost::asio::io_context& executor_;

    std::list<std::vector<uint8_t>> send_que_;

    client* self_;

    std::string host_ = "127.0.0.1";
    uint16_t port_    = 5900;

    /** negotiated protocol version */
    int major_ = rfbProtocolMajorVersion, minor_ = rfbProtocolMinorVersion;

    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;

    client::get_password_handler_type get_password_handler_;

    struct rfbSupportedMessages
    {
        uint8_t client2server[32] = {0}; /* maximum of 256 message types (256/8)=32 */
        uint8_t server2client[32] = {0}; /* maximum of 256 message types (256/8)=32 */
    };
    rfbSupportedMessages supportedMessages_;

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


    struct rfbPixelFormat
    {
        uint8_t bitsPerPixel; /* 8,16,32 only */

        uint8_t depth; /* 8 to 32 */

        uint8_t bigEndian; /* True if multi-byte pixels are interpreted
                              as big endian, or if single-bit-per-pixel
                              has most significant bit of the byte
                              corresponding to first (leftmost) pixel. Of
                              course this is meaningless for 8 bits/pix */

        uint8_t trueColour; /* If false then we need a "colour map" to
                               convert pixels to RGB.  If true, xxxMax and
                               xxxShift specify bits used for red, green
                               and blue */

        /* the following fields are only meaningful if trueColour is true */

        uint16_t redMax; /* maximum red value (= 2^n - 1 where n is the
                            number of bits used for red). Note this
                            value is always in big endian order. */

        uint16_t greenMax; /* similar for green */

        uint16_t blueMax; /* and blue */

        uint8_t redShift; /* number of shifts needed to get the red
                             value in a pixel to the least significant
                             bit. To find the red value from a given
                             pixel, do the following:
                             1) Swap pixel value according to bigEndian
                                (e.g. if bigEndian is false and host byte
                                order is big endian, then swap).
                             2) Shift right by redShift.
                             3) AND with redMax (in host byte order).
                             4) You now have the red value between 0 and
                                redMax. */

        uint8_t greenShift; /* similar for green */

        uint8_t blueShift; /* and blue */

        uint8_t pad1;
        uint16_t pad2;

        void print() const
        {
            if (bitsPerPixel == 1) {
                spdlog::info("  Single bit per pixel.");
                spdlog::info("  {} significant bit in each byte is leftmost on the screen.",
                             (bigEndian ? "Most" : "Least"));
            }
            else {
                spdlog::info("  {} bits per pixel.", bitsPerPixel);
                if (bitsPerPixel != 8) {
                    spdlog::info("  {} significant byte first in each pixel.",
                                 (bigEndian ? "Most" : "Least"));
                }
                if (trueColour) {
                    spdlog::info("  TRUE colour: max red {} green {} blue {}"
                                 ", shift red {} green {} blue {}",
                                 redMax,
                                 greenMax,
                                 blueMax,
                                 redShift,
                                 greenShift,
                                 blueShift);
                }
                else {
                    spdlog::info("  Colour map (not true colour).");
                }
            }
        }
    };

    struct rfbServerInitMsg
    {
        uint16_t framebufferWidth;
        uint16_t framebufferHeight;
        rfbPixelFormat format; /* the server's preferred pixel format */
        uint32_t nameLength;
        /* followed by char name[nameLength] */
    };

    rfbServerInitMsg si;

    std::string desktopName_;
};
} // namespace libvnc