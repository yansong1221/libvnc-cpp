#include "client_impl.h"

#include "use_awaitable.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/detached.hpp>

#include <boost/asio/streambuf.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>
#include <openssl/des.h> // DES 加密函数
#include <openssl/dh.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/rand.h> // 随机数生成（用于挑战值）
#include <openssl/sha.h>
#include <ranges>
#include <spdlog/spdlog.h>
#include <string.h>

#if (OPENSSL_VERSION_NUMBER >= 0x30000000L)
#include <openssl/provider.h>
#endif

#include "libvnc-cpp/client.h"
#include "libvnc-cpp/error.h"
#include "libvnc-cpp/proto.h"

#include "encoding/copy_rect.hpp"
#include "encoding/cursor.hpp"
#include "encoding/keyboard_led_state.hpp"
#include "encoding/new_fb_size.hpp"
#include "encoding/pointer_pos.hpp"
#include "encoding/raw.hpp"
#include "encoding/rre.hpp"

#if defined(LIBVNC_HAVE_LIBZ)
#include <zstr.hpp>
#endif

#include "d3des.hpp"
#include <filesystem>
#include <iostream>
#include <ranges>

namespace libvnc {

namespace detail {

static void rfbEncryptBytes(uint8_t* challenge, const char* passwd)
{
    unsigned char key[8];
    unsigned int i;

    /* key is simply password padded with nulls */

    for (i = 0; i < 8; i++) {
        if (i < strlen(passwd)) {
            key[i] = passwd[i];
        }
        else {
            key[i] = 0;
        }
    }

    deskey(key, EN0);

    for (i = 0; i < 16; i += 8) {
        des(challenge + i, challenge + i);
    }
}


} // namespace detail

client_impl::client_impl(const boost::asio::any_io_executor& executor,
                         client_delegate* handler,
                         std::string_view host,
                         uint16_t port)
    : strand_(executor)
    , socket_(executor)
    , resolver_(executor)
    , host_(host)
    , port_(port)
    , handler_(handler)
{
    frame_codecs_.push_back(std::make_unique<encoding::rre>());
    frame_codecs_.push_back(std::make_unique<encoding::co_rre>());
    frame_codecs_.push_back(std::make_unique<encoding::raw>());
    frame_codecs_.push_back(std::make_unique<encoding::copy_rect>());


    codecs_.push_back(std::make_unique<encoding::x_cursor>());
    codecs_.push_back(std::make_unique<encoding::rich_cursor>());
    codecs_.push_back(std::make_unique<encoding::keyboard_led_state>());
    codecs_.push_back(std::make_unique<encoding::new_fb_size>());
    codecs_.push_back(std::make_unique<encoding::pointer_pos>());
}

const libvnc::frame_buffer& client_impl::frame() const
{
    return buffer_;
}

void client_impl::close()
{
    resolver_.cancel();
    if (socket_.is_open()) {
        boost::system::error_code ec;
        socket_.shutdown(boost::asio::socket_base::shutdown_both, ec);
        socket_.close(ec);
    }
}
void client_impl::start()
{
    boost::asio::co_spawn(
        strand_,
        [this, self = shared_from_this()]() -> boost::asio::awaitable<void> {
            auto err = co_await async_connect_rfbserver();
            handler_->on_connect(err);
            if (err)
                co_return;

            boost::system::error_code ec;
            auto remote_endp = socket_.remote_endpoint(ec);

            try {
                co_await server_message_loop();
            }
            catch (const boost::system::system_error& e) {
                ec = e.code();
                spdlog::error("Disconnect from the rbfserver [{}:{}] : {}",
                              remote_endp.address().to_string(),
                              remote_endp.port(),
                              ec.message());
            }
            catch (const std::exception& e) {
                spdlog::error("Disconnect from the rbfserver [{}:{}] : {}",
                              remote_endp.address().to_string(),
                              remote_endp.port(),
                              e.what());
            }
            if (handler_)
                handler_->on_disconnect(error::make_error(ec));
        },
        boost::asio::detached);
}

void client_impl::send_framebuffer_update_request(int x, int y, int w, int h, bool incremental)
{
    proto::rfbFramebufferUpdateRequestMsg msg {};
    msg.x           = x;
    msg.y           = y;
    msg.w           = w;
    msg.h           = h;
    msg.incremental = incremental;
    send_msg_to_server(proto::rfbFramebufferUpdateRequest, &msg, sizeof(msg));
}

void client_impl::send_framebuffer_update_request(bool incremental)
{
    return send_framebuffer_update_request(0, 0, buffer_.width(), buffer_.width(), incremental);
}

std::vector<std::string> client_impl::supported_encodings() const
{
    std::vector<std::string> encs;
    for (const auto& item : frame_codecs_)
        encs.push_back(item->codec_name());

    return encs;
}

void client_impl::send_pointer_event(int x, int y, int buttonMask)
{
    proto::rfbPointerEventMsg pe {};
    pe.buttonMask = buttonMask;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;

    send_msg_to_server(proto::rfbPointerEvent, &pe, sizeof(pe));
}

void client_impl::send_key_event(uint32_t key, bool down)
{
    proto::rfbKeyEventMsg ke {};
    ke.down = down ? 1 : 0;
    ke.key  = key;
    send_msg_to_server(proto::rfbKeyEvent, &ke, sizeof(ke));
}

boost::asio::awaitable<error> client_impl::async_connect_rfbserver()
{
    close();

    // connect
    boost::system::error_code ec;
    auto endpoints =
        co_await resolver_.async_resolve(host_, std::to_string(port_), net_awaitable[ec]);
    if (ec) {
        spdlog::error("Failed to resolve [{}:{}] : {}", host_, port_, ec.message());
        co_return ec;
    }
    co_await boost::asio::async_connect(socket_, endpoints, net_awaitable[ec]);
    if (ec) {
        spdlog::error("Failed to connect rfbserver [{}:{}] : {}", host_, port_, ec.message());
        co_return ec;
    }

    // handshake
    proto::rfbProtocolVersionMsg pv = {0};
    co_await boost::asio::async_read(socket_,
                                     boost::asio::buffer(&pv, sizeof(pv)),
                                     boost::asio::transfer_exactly(proto::sz_rfbProtocolVersionMsg),
                                     net_awaitable[ec]);
    if (ec) {
        spdlog::error(
            "Failed to read rfbserver handshake data [{}:{}] : {}", host_, port_, ec.message());
        co_return ec;
    }

    int major, minor;
    if (sscanf(pv, proto::rfbProtocolVersionFormat, &major, &minor) != 2) {
        spdlog::error("Not a valid VNC server ({})", pv);
        co_return boost::system::errc::make_error_code(boost::system::errc::wrong_protocol_type);
    }

    major_ = major;
    minor_ = minor;

    supported_messages_.reset();

    /* fall back to viewer supported version */
    if ((major == proto::rfbProtocolMajorVersion) && (minor > proto::rfbProtocolMinorVersion))
        minor_ = proto::rfbProtocolMinorVersion;

    /* Legacy version of UltraVNC uses minor codes 4 and 6 for the server */
    /* left in for backwards compatibility */
    if (major == 3 && (minor == 4 || minor == 6)) {
        spdlog::info("UltraVNC server detected, enabling UltraVNC specific messages: {}", pv);
        supported_messages_.supported_ultra_vnc();
    }

    /* Legacy version of UltraVNC Single Click uses minor codes 14 and 16 for the server */
    /* left in for backwards compatibility */
    if (major == 3 && (minor == 14 || minor == 16)) {
        minor -= 10;
        minor_ = minor;
        spdlog::info(
            "UltraVNC Single Click server detected, enabling UltraVNC specific messages: {}", pv);
        supported_messages_.supported_ultra_vnc();
    }

    /* Legacy version of TightVNC uses minor codes 5 for the server */
    /* left in for backwards compatibility */
    if (major == 3 && minor == 5) {
        spdlog::info("TightVNC server detected, enabling TightVNC specific messages: {}", pv);
        supported_messages_.supported_tight_vnc();
    }

    /* we do not support > RFB3.8 */
    if ((major == 3 && minor > 8) || major > 3) {
        major_ = 3;
        minor_ = 8;
    }
    spdlog::info("VNC server supports protocol version {}.{} (viewer {}.{})",
                 major,
                 minor,
                 proto::rfbProtocolMajorVersion,
                 proto::rfbProtocolMinorVersion);

    sprintf(pv, proto::rfbProtocolVersionFormat, major_, minor_);
    co_await boost::asio::async_write(
        socket_, boost::asio::buffer(&pv, proto::sz_rfbProtocolVersionMsg), net_awaitable[ec]);

    if (ec) {
        spdlog::error(
            "Failed to send rfbserver handshake data [{}:{}] : {}", host_, port_, ec.message());
        co_return ec;
    }


    if (auto err = co_await async_authenticate(); err) {
        spdlog::error("Authentication with the server failed: {}", err.message());
        co_return ec;
    }


    try {
        co_await async_client_init();
    }
    catch (const boost::system::system_error& e) {
        ec = e.code();
        spdlog::error("Failed to initialize the client: {}", ec.message());
        co_return ec;
    }

    co_return ec;
}

boost::asio::awaitable<error> client_impl::async_authenticate()
{
    proto::rfbAuthScheme selected_auth_scheme = proto::rfbConnFailed;

    /* 3.7 and onwards sends a # of security types first */
    if (major_ == 3 && minor_ > 6) {
        boost::system::error_code ec;

        uint8_t count = 0;
        co_await boost::asio::async_read(
            socket_, boost::asio::buffer(&count, sizeof(count)), net_awaitable[ec]);
        if (ec)
            co_return ec;

        std::vector<proto::rfbAuthScheme> tAuth(count);
        co_await boost::asio::async_read(socket_, boost::asio::buffer(tAuth), net_awaitable[ec]);
        if (ec)
            co_return ec;

        selected_auth_scheme = handler_->select_auth_scheme({tAuth.begin(), tAuth.end()});
        co_await boost::asio::async_write(
            socket_, boost::asio::buffer(&selected_auth_scheme, 1), net_awaitable[ec]);
        if (ec)
            co_return ec;
    }
    else {
        boost::system::error_code ec;
        boost::endian::big_uint32_buf_t authScheme {};
        co_await boost::asio::async_read(
            socket_, boost::asio::buffer(&authScheme, sizeof(authScheme)), net_awaitable[ec]);
        if (ec)
            co_return ec;

        selected_auth_scheme = static_cast<proto::rfbAuthScheme>(authScheme.value());
    }

    switch (selected_auth_scheme) {
        case proto::rfbConnFailed: break;
        case proto::rfbNoAuth: {
            spdlog::info("No authentication needed");

            /* 3.8 and upwards sends a Security Result for rfbNoAuth */
            if ((major_ == 3 && minor_ > 7) || major_ > 3)
                co_return co_await read_auth_result();
            else
                co_return boost::system::error_code {};

        } break;
        case proto::rfbVncAuth: {
            boost::system::error_code ec;
            std::vector<uint8_t> challenge;
            challenge.resize(16);
            co_await boost::asio::async_read(
                socket_, boost::asio::buffer(challenge), net_awaitable[ec]);
            if (ec)
                co_return ec;

            auto password = handler_->get_auth_password();
            detail::rfbEncryptBytes(challenge.data(), password.c_str());

            co_await boost::asio::async_write(
                socket_, boost::asio::buffer(challenge), net_awaitable[ec]);
            if (ec)
                co_return ec;

            co_return co_await read_auth_result();

        } break;
        case proto::rfbRA2: break;
        case proto::rfbRA2ne: break;
        case proto::rfbSSPI: break;
        case proto::rfbSSPIne: break;
        case proto::rfbTight: break;
        case proto::rfbUltra: break;
        case proto::rfbTLS: break;
        case proto::rfbVeNCrypt: break;
        case proto::rfbSASL: break;
        case proto::rfbARD: break;
        case proto::rfbUltraMSLogonI: break;
        case proto::rfbUltraMSLogonII: break;
        case proto::rfbUltraVNC_SecureVNCPluginAuth: break;
        case proto::rfbUltraVNC_SecureVNCPluginAuth_new: break;
        case proto::rfbClientInitExtraMsgSupport: break;
        case proto::rfbClientInitExtraMsgSupportNew: break;
        default: break;
    }
    co_return custom_error {
        custom_error::auth_error,
        fmt::format("Unimplemented authentication method: {}! ", (int)selected_auth_scheme)};
}

boost::asio::awaitable<error> client_impl::async_client_init()
{
    boost::system::error_code ec;

    uint8_t shared = app_data_.shareDesktop ? 1 : 0;
    co_await boost::asio::async_write(
        socket_, boost::asio::buffer(&shared, sizeof(shared)), net_awaitable[ec]);
    if (ec)
        co_return ec;

    proto::rfbServerInitMsg si {};
    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&si, sizeof(si)), net_awaitable[ec]);
    if (ec)
        co_return ec;

    if (si.nameLength.value() > 1 << 20) {
        auto msg = fmt::format("Too big desktop name length sent by server: {} B > 1 MB",
                               (unsigned int)si.nameLength.value());
        spdlog::error(msg);
        co_return custom_error {custom_error::client_init_error, msg};
    }

    std::string name;
    name.resize(si.nameLength.value());
    co_await boost::asio::async_read(socket_, boost::asio::buffer(name), net_awaitable[ec]);
    if (ec)
        co_return ec;

    spdlog::info("Desktop name \"{}\"", name);
    spdlog::info("Connected to VNC server, using protocol version {}.{}", major_, minor_);
    spdlog::info("VNC server default format:");
    si.format.print();

    buffer_.init(si.framebufferWidth.value(), si.framebufferHeight.value(), si.format);

    desktop_name_ = std::move(name);

    set_format(handler_->want_format());
    set_encodings(supported_encodings());

    send_framebuffer_update_request(false);
}

void client_impl::set_format(const proto::rfbPixelFormat& format)
{
    proto::rfbSetPixelFormatMsg spf {};
    spf.pad1   = 0;
    spf.pad2   = 0;
    spf.format = format;

    if (send_msg_to_server(proto::rfbSetPixelFormat, &spf, sizeof(spf)))
        buffer_.set_format(format);
}

void client_impl::set_encodings(const std::vector<std::string>& encodings)
{
    boost::asio::dispatch(strand_, [this, self = shared_from_this(), encodings]() {
        std::vector<boost::endian::big_uint32_buf_t> encs;

        bool requestCompressLevel    = false;
        bool requestQualityLevel     = false;
        bool requestLastRectEncoding = false;
        for (const auto& codec_name : encodings) {
            auto iter = std::ranges::find_if(
                frame_codecs_, [&](const auto& enc) { return enc->codec_name() == codec_name; });
            if (iter == frame_codecs_.end())
                continue;
            encs.emplace_back((*iter)->encoding_code());

            if ((*iter)->requestCompressLevel())
                requestCompressLevel = true;
            if ((*iter)->requestQualityLevel())
                requestQualityLevel = true;
            if ((*iter)->requestLastRectEncoding())
                requestLastRectEncoding = true;
        }

        if (requestCompressLevel)
            encs.emplace_back(app_data_.compressLevel + proto::rfbEncodingCompressLevel0);

        if (requestQualityLevel)
            encs.emplace_back(app_data_.qualityLevel + proto::rfbEncodingQualityLevel0);

        if (requestLastRectEncoding)
            encs.emplace_back(proto::rfbEncodingLastRect);

        for (const auto& enc : codecs_)
            encs.emplace_back(enc->encoding_code());

#ifdef LIBVNC_HAVE_LIBZ
        encs.emplace_back(proto::rfbEncodingExtendedClipboard);
#endif

        proto::rfbSetEncodingsMsg msg {};
        msg.pad        = 0;
        msg.nEncodings = encs.size();

        boost::asio::streambuf buffer;
        std::ostream os(&buffer);
        os.write((char*)&msg, sizeof(msg));

        for (const auto& enc : encs)
            os.write((char*)&enc, sizeof(enc));

        send_msg_to_server(proto::rfbSetEncodings, buffer.data().data(), buffer.size());
    });
}

boost::asio::awaitable<void> client_impl::server_message_loop()
{
    for (;;) {
        proto::rfbServerToClientMsg msg_id {};
        co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg_id, sizeof(msg_id)));

        switch (msg_id) {
            case proto::rfbFramebufferUpdate: {
                proto::rfbFramebufferUpdateMsg msg {};
                co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)));
                co_await on_message(msg);
            } break;
            case proto::rfbSetColourMapEntries: break;
            case proto::rfbBell: {
                if (handler_)
                    handler_->on_bell();
            } break;
            case proto::rfbServerCutText: {
                proto::rfbServerCutTextMsg msg {};
                co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)));
                co_await on_message(msg);
            } break;
            case proto::rfbTextChat: {
                proto::rfbTextChatMsg msg {};
                co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)));
                co_await on_message(msg);
            } break;
            case proto::rfbXvp: {
                proto::rfbXvpMsg msg {};
                co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)));
                co_await on_message(msg);
            } break;
            case proto::rfbResizeFrameBuffer: {
                proto::rfbResizeFrameBufferMsg msg {};
                co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)));
                co_await on_message(msg);

            } break;
            case proto::rfbPalmVNCReSizeFrameBuffer: {
                proto::rfbPalmVNCReSizeFrameBufferMsg msg {};
                co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)));
                co_await on_message(msg);
            } break;
            case proto::rfbServerState: break;
            default: {
                spdlog::error("Unknown message type {} from VNC server", (int)msg_id);
            } break;
        }
    }
}

boost::asio::awaitable<error> client_impl::read_auth_result()
{
    boost::system::error_code ec;
    boost::endian::big_uint32_buf_t authResult {};
    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&authResult, sizeof(authResult)), net_awaitable[ec]);
    if (ec)
        co_return ec;

    switch (authResult.value()) {
        case proto::rfbVncAuthOK: {
            spdlog::info("VNC authentication succeeded");
            co_return ec;
        } break;
        case proto::rfbVncAuthFailed: {
            if (major_ == 3 && minor_ > 7) {
                /* we have an error following */
                co_return co_await read_error_reason();
            }
        } break;
        case proto::rfbVncAuthTooMany: {
            std::string msg = "VNC authentication failed - too many tries";
            spdlog::error(msg);
            co_return error::make_error(custom_error::auth_error, msg);
        } break;
        default: break;
    }
    auto msg = std::format("Unknown VNC authentication result: {}", authResult.value());
    spdlog::error(msg);
    co_return error::make_error(custom_error::auth_error, msg);
}

boost::asio::awaitable<error> client_impl::read_error_reason()
{
    boost::system::error_code ec;
    boost::endian::big_uint32_buf_t reasonLen {};
    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&reasonLen, sizeof(reasonLen)), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    if (reasonLen.value() > 1 << 20) {
        auto msg =
            fmt::format("VNC connection failed, but sent reason length of {} exceeds limit of 1MB",
                        reasonLen.value());
        spdlog::error(msg);
        co_return error::make_error(custom_error::auth_error, msg);
    }

    std::string reason;
    reason.resize(reasonLen.value());

    co_await boost::asio::async_read(socket_, boost::asio::buffer(reason), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    spdlog::error("VNC connection failed: {}", reason);
    co_return error::make_error(custom_error::auth_error, reason);
}

bool client_impl::send_msg_to_server(const proto::rfbClientToServerMsg& ID,
                                     const void* data,
                                     std::size_t len)
{
    if (!supported_messages_.supports_client2server(ID)) {
        spdlog::warn("Unsupported client2server protocol: {}", (int)ID);
        return false;
    }

    std::vector<uint8_t> buffer;
    buffer.reserve(len + sizeof(ID));
    buffer.push_back(ID);
    std::copy((uint8_t*)data, (uint8_t*)data + len, std::back_inserter(buffer));
    send_raw_data(std::move(buffer));
    return true;
}

void client_impl::send_raw_data(const std::span<uint8_t>& data)
{
    send_raw_data(std::vector<uint8_t>(data.begin(), data.end()));
}

void client_impl::send_raw_data(std::vector<uint8_t>&& data)
{
    boost::asio::dispatch(strand_, [this, self = shared_from_this(), data = std::move(data)]() {
        bool write_in_proccess = !send_que_.empty();
        send_que_.push_back(std::move(data));
        if (write_in_proccess)
            return;

        boost::asio::co_spawn(
            strand_,
            [this, self = shared_from_this()]() -> boost::asio::awaitable<void> {
                while (!send_que_.empty()) {
                    const auto& buffer = send_que_.front();

                    boost::system::error_code ec;
                    co_await boost::asio::async_write(
                        socket_, boost::asio::buffer(buffer), net_awaitable[ec]);
                    if (ec) {
                        send_que_.clear();
                        co_return;
                    }
                    send_que_.pop_front();
                }
            },
            boost::asio::detached);
    });
}


encoding::codec* client_impl::find_encoding(const proto::rfbEncoding& encoding)
{
    {
        auto iter = std::ranges::find_if(
            codecs_, [&](const auto& codec) { return codec->encoding_code() == encoding; });
        if (iter != codecs_.end()) {
            return iter->get();
        }
    }

    {
        auto iter = std::ranges::find_if(
            frame_codecs_, [&](const auto& codec) { return codec->encoding_code() == encoding; });
        if (iter != frame_codecs_.end()) {
            return iter->get();
        }
    }
    return nullptr;
}

void client_impl::got_bitmap(const uint8_t* buffer, int x, int y, int w, int h)
{
    buffer_.got_bitmap(buffer, x, y, w, h);
}

void client_impl::got_copy_rect(int src_x, int src_y, int w, int h, int dest_x, int dest_y)
{
    buffer_.got_copy_rect(src_x, src_y, w, h, dest_x, dest_y);
}

void client_impl::got_fill_rect(int x, int y, int w, int h, uint32_t colour)
{
    buffer_.got_fill_rect(x, y, w, h, colour);
}

void client_impl::resize_client_buffer(int width, int height)
{
    buffer_.set_size(width, height);

    send_framebuffer_update_request(false);
    spdlog::info("Got new framebuffer size: {}x{}", width, height);
}

boost::asio::awaitable<void> client_impl::on_message(const proto::rfbFramebufferUpdateMsg& msg)
{
    proto::rfbFramebufferUpdateRectHeader UpdateRect {};
    for (int i = 0; i < msg.num_rects.value(); ++i) {
        co_await boost::asio::async_read(socket_,
                                         boost::asio::buffer(&UpdateRect, sizeof(UpdateRect)));

        auto encoding = (proto::rfbEncoding)UpdateRect.encoding.value();
        switch (encoding) {
            case proto::rfbEncodingLastRect: co_return; break;
            case proto::rfbEncodingExtDesktopSize: {
                proto::rfbExtDesktopSizeMsg eds;
                co_await boost::asio::async_read(socket_, boost::asio::buffer(&eds, sizeof(eds)));

                rfbExtDesktopScreen screen;
                auto screens = eds.numberOfScreens.value();
                for (int loop = 0; loop < screens; loop++) {
                    co_await boost::asio::async_read(socket_,
                                                     boost::asio::buffer(&screen, sizeof(screen)));
                }

            } break;
                /* rect.r.w=byte count */
            case proto::rfbEncodingSupportedMessages: {
                proto::rfbSupportedMessages supportedMessages = {0};
                co_await boost::asio::async_read(
                    socket_, boost::asio::buffer(&supportedMessages, sizeof(supportedMessages)));
                supported_messages_.assign(supportedMessages);
                supported_messages_.print();

            } break;
                /* rect.r.w=byte count, rect.r.h=# of encodings */
            case proto::rfbEncodingSupportedEncodings: {
                std::vector<uint8_t> buffer;
                buffer.resize(UpdateRect.r.w.value());

                co_await boost::asio::async_read(socket_, boost::asio::buffer(buffer));

                /* buffer now contains rect.r.h # of uint32_t encodings that the server supports */
                /* currently ignored by this library */

            } break;
                /* rect.r.w=byte count */
            case proto::rfbEncodingServerIdentity: {
                std::string buffer;
                buffer.resize(UpdateRect.r.w.value());

                co_await boost::asio::async_read(socket_, boost::asio::buffer(buffer));

                spdlog::info("Connected to Server \"{}\"", buffer);
            } break;

            default: {
                auto enc = find_encoding((proto::rfbEncoding)UpdateRect.encoding.value());
                if (!enc)
                    throw;

                co_await enc->decode(
                    socket_, UpdateRect.r, buffer_.pixel_format(), shared_from_this());

            } break;
        }
    }
    if (handler_)
        handler_->on_frame_update(buffer_);
    send_framebuffer_update_request(true);
}

boost::asio::awaitable<void> client_impl::on_message(const proto::rfbTextChatMsg& msg)
{
    using namespace std::string_view_literals;

    switch (msg.length.value()) {
        case proto::rfbTextChatOpen: {
            spdlog::info("Received TextChat Open");
            handler_->on_text_chat(proto::rfbTextChatOpen, ""sv);
        } break;
        case proto::rfbTextChatClose: {
            spdlog::info("Received TextChat Close");
            handler_->on_text_chat(proto::rfbTextChatClose, ""sv);
        } break;
        case proto::rfbTextChatFinished: {
            spdlog::info("Received TextChat Finished");
            handler_->on_text_chat(proto::rfbTextChatFinished, ""sv);
        } break;
        default: {
            std::string message;
            message.resize(msg.length.value());
            co_await boost::asio::async_read(socket_, boost::asio::buffer(message));

            spdlog::info("Received TextChat \"{}\"", message);
            handler_->on_text_chat(proto::rfbTextChatMessage, message);
        } break;
    }
    co_return;
}

boost::asio::awaitable<void> client_impl::on_message(const proto::rfbXvpMsg& msg)
{
    supported_messages_.set_client2server(proto::rfbXvp);
    supported_messages_.set_server2client(proto::rfbXvp);
    co_return;
}

boost::asio::awaitable<void> client_impl::on_message(const proto::rfbResizeFrameBufferMsg& msg)
{
    resize_client_buffer(msg.framebufferWidth.value(), msg.framebufferHeight.value());
    co_return;
}

boost::asio::awaitable<void>
client_impl::on_message(const proto::rfbPalmVNCReSizeFrameBufferMsg& msg)
{
    resize_client_buffer(msg.buffer_w.value(), msg.buffer_h.value());
    co_return;
}

boost::asio::awaitable<void> client_impl::on_message(const proto::rfbServerCutTextMsg& msg)
{
    int32_t ilen = msg.length.value();

    ilen = ilen < 0 ? -ilen : ilen;

    boost::asio::streambuf buffer;
    co_await boost::asio::async_read(socket_, buffer, boost::asio::transfer_exactly(ilen));
    std::istream input_stream(&buffer);
    std::string text;
#if defined(LIBVNC_HAVE_LIBZ)

    boost::endian::big_uint32_buf_t flags {};
    input_stream.read((char*)&flags, sizeof(flags));

    /*
     * only process (text | provide). Ignore all others
     * modify here if need more types(rtf,html,dib,files)
     */
    if (!(flags.value() & proto::rfbExtendedClipboard_Text)) {
        spdlog::info("rfbServerCutTextMsg. not text type. ignore");
        co_return;
    }
    if (!(flags.value() & proto::rfbExtendedClipboard_Provide)) {
        spdlog::info("rfbServerCutTextMsg. not provide type. ignore");
        co_return;
    }
    if (flags.value() & proto::rfbExtendedClipboard_Caps) {
        spdlog::info("rfbServerCutTextMsg. default cap.");
        // client->extendedClipboardServerCapabilities |=
        //     rfbExtendedClipboard_Text; /* for now, only text */
        co_return;
    }

    boost::endian::big_uint32_buf_t size {};
    zstr::istream zs(input_stream);
    if (!zs.read((char*)&size, sizeof(size))) {
        spdlog::error("rfbServerCutTextMsg. inflate size failed");
        co_return;
    }

    if (size.value() > (1 << 20)) {
        spdlog::error("rfbServerCutTextMsg. size too large");
        co_return;
    }
    text.resize(size.value());
    if (!zs.read(text.data(), text.size())) {
        spdlog::error("rfbServerCutTextMsg. inflate buf failed");
        co_return;
    }
#else
    text.resize(ilen);
    input_stream.read(text.data(), text.size());
#endif
    spdlog::info("Got server cut text: {}", std::filesystem::u8path(text).string());
    co_return;
}

} // namespace libvnc