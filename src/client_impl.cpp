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
#include "encoding/ext_desktop_size.hpp"
#include "encoding/hextile.hpp"
#include "encoding/server_identity.hpp"
#include "encoding/supported_encodings.hpp"
#include "encoding/supported_messages.hpp"
#include "encoding/ultra.hpp"
#include <boost/asio/experimental/awaitable_operators.hpp>
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

client_impl::client_impl(const boost::asio::any_io_executor& executor)
    : strand_(executor)
    , socket_(executor)
    , resolver_(executor)
{
    message_map_[proto::rfbFramebufferUpdate] =
        std::bind(&client_impl::on_rfbFramebufferUpdate, this);
    message_map_[proto::rfbSetColourMapEntries] =
        std::bind(&client_impl::on_rfbSetColourMapEntries, this);
    message_map_[proto::rfbBell]          = std::bind(&client_impl::on_rfbBell, this);
    message_map_[proto::rfbServerCutText] = std::bind(&client_impl::on_rfbServerCutText, this);
    message_map_[proto::rfbTextChat]      = std::bind(&client_impl::on_rfbTextChat, this);
    message_map_[proto::rfbXvp]           = std::bind(&client_impl::on_rfbXvp, this);
    message_map_[proto::rfbResizeFrameBuffer] =
        std::bind(&client_impl::on_rfbResizeFrameBuffer, this);
    message_map_[proto::rfbPalmVNCReSizeFrameBuffer] =
        std::bind(&client_impl::on_rfbPalmVNCReSizeFrameBuffer, this);


    codecs_.push_back(std::make_unique<encoding::ultra>());
    codecs_.push_back(std::make_unique<encoding::copy_rect>());
    codecs_.push_back(std::make_unique<encoding::raw>());
    codecs_.push_back(std::make_unique<encoding::co_rre>());
    codecs_.push_back(std::make_unique<encoding::rre>());
    codecs_.push_back(std::make_unique<encoding::hextile>());

    codecs_.push_back(std::make_unique<encoding::x_cursor>());
    codecs_.push_back(std::make_unique<encoding::rich_cursor>());
    codecs_.push_back(std::make_unique<encoding::keyboard_led_state>());
    codecs_.push_back(std::make_unique<encoding::new_fb_size>());
    codecs_.push_back(std::make_unique<encoding::pointer_pos>());
    codecs_.push_back(std::make_unique<encoding::server_identity>());
    codecs_.push_back(std::make_unique<encoding::supported_encodings>());
    codecs_.push_back(std::make_unique<encoding::ext_desktop_size>());
    codecs_.push_back(std::make_unique<encoding::supported_messages>());
}

const libvnc::frame_buffer& client_impl::frame() const
{
    return frame_;
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
            error err = co_await co_start();
        },
        boost::asio::detached);
}

boost::asio::awaitable<libvnc::error> client_impl::co_start()
{
    boost::system::error_code ec;
    is_initialization_completed_ = false;

    error err = co_await async_connect_rfbserver();
    co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
    if (!err) {
        is_initialization_completed_ = true;
        if (handler_)
            send_format(handler_->want_format());
        send_frame_encodings(supported_frame_encodings());
        send_framebuffer_update_request(false);
    }
    if (handler_)
        handler_->on_connect(err);
    if (err)
        co_return err;


    auto remote_endp = socket_.remote_endpoint(ec);

    err = co_await server_message_loop();
    if (err) {
        spdlog::error("Disconnect from the rbfserver [{}:{}] : {}",
                      remote_endp.address().to_string(),
                      remote_endp.port(),
                      err.message());
    }
    is_initialization_completed_ = false;

    co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
    if (handler_)
        handler_->on_disconnect(err);
    co_return err;
}

void client_impl::set_delegate(client_delegate* handler)
{
    boost::asio::dispatch(strand_,
                          [this, self = shared_from_this(), handler]() { handler_ = handler; });
}

int client_impl::current_keyboard_led_state() const
{
    return current_keyboard_led_state_;
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
    return send_framebuffer_update_request(0, 0, frame_.width(), frame_.height(), incremental);
}

std::vector<std::string> client_impl::supported_frame_encodings() const
{
    std::vector<std::string> encs;
    for (const auto& item : codecs_) {
        if (!item->is_frame_codec())
            continue;
        encs.push_back(item->codec_name());
    }
    return encs;
}

bool client_impl::send_pointer_event(int x, int y, int buttonMask)
{
    proto::rfbPointerEventMsg pe {};
    pe.buttonMask = buttonMask;
    pe.x          = std::max(x, 0);
    pe.y          = std::max(y, 0);
    return send_msg_to_server(proto::rfbPointerEvent, &pe, sizeof(pe));
}

bool client_impl::send_client_cut_text(std::string_view text)
{
    proto::rfbClientCutTextMsg cct {};
    cct.pad1   = 0;
    cct.pad2   = 0;
    cct.length = (int)text.size();

    return send_msg_to_server_buffers(
        proto::rfbClientCutText, boost::asio::buffer(&cct, sizeof(cct)), text);
}

bool client_impl::send_client_cut_text_utf8(std::string_view text)
{
    if (!extendedClipboardServerCapabilities_.any())
        return false;
#if defined(LIBVNC_HAVE_LIBZ)
    boost::endian::big_uint32_buf_t flags {};
    flags = proto::rfbExtendedClipboard_Provide | proto::rfbExtendedClipboard_Text;

    boost::asio::streambuf compress_buffer;
    std::ostream out_os(&compress_buffer);

    {
        boost::endian::big_uint32_buf_t text_size {};
        text_size = (uint32_t)text.size();

        zstr::ostream z_os(out_os, zstr::default_buff_size, -1, 15);
        z_os.write((char*)&text_size, sizeof(text_size));
        z_os.write(text.data(), text.size());
    }
    int len = sizeof(flags) + compress_buffer.size();

    proto::rfbClientCutTextMsg cct {};
    cct.pad1   = 0;
    cct.pad2   = 0;
    cct.length = -len;

    return send_msg_to_server_buffers(proto::rfbClientCutText,
                                      boost::asio::buffer(&cct, sizeof(cct)),
                                      boost::asio::buffer(&flags, sizeof(flags)),
                                      compress_buffer.data());
#else
    return false;
#endif
}

bool client_impl::text_chat_send(std::string_view text)
{
    proto::rfbTextChatMsg chat {};
    chat.pad1   = 0;
    chat.pad2   = 0;
    chat.length = (int)text.length();
    return send_msg_to_server_buffers(
        proto::rfbTextChat, boost::asio::buffer(&chat, sizeof(chat)), text);
}

bool client_impl::text_chat_open()
{
    proto::rfbTextChatMsg chat {};
    chat.pad1   = 0;
    chat.pad2   = 0;
    chat.length = proto::rfbTextChatOpen;
    return send_msg_to_server(proto::rfbTextChat, &chat, sizeof(chat));
}

bool client_impl::text_chat_close()
{
    proto::rfbTextChatMsg chat {};
    chat.pad1   = 0;
    chat.pad2   = 0;
    chat.length = proto::rfbTextChatClose;
    return send_msg_to_server(proto::rfbTextChat, &chat, sizeof(chat));
}

bool client_impl::text_chat_finish()
{
    proto::rfbTextChatMsg chat {};
    chat.pad1   = 0;
    chat.pad2   = 0;
    chat.length = proto::rfbTextChatFinished;
    return send_msg_to_server(proto::rfbTextChat, &chat, sizeof(chat));
}

bool client_impl::permit_server_input(bool enabled)
{
    proto::rfbSetServerInputMsg msg {};
    msg.pad    = 0;
    msg.status = enabled ? 1 : 0;
    return send_msg_to_server(proto::rfbSetServerInput, &msg, sizeof(msg));
}

bool client_impl::send_xvp_msg(uint8_t version, proto::rfbXvpCode code)
{
    proto::rfbXvpMsg xvp {};
    xvp.pad     = 0;
    xvp.version = version;
    xvp.code    = code;
    return send_msg_to_server(proto::rfbXvp, &xvp, sizeof(xvp));
}

bool client_impl::send_key_event(uint32_t key, bool down)
{
    proto::rfbKeyEventMsg ke {};
    ke.down = down ? 1 : 0;
    ke.key  = key;
    return send_msg_to_server(proto::rfbKeyEvent, &ke, sizeof(ke));
}

bool client_impl::send_extended_key_event(uint32_t keysym, uint32_t keycode, bool down)
{
    proto::rfbQemuExtendedKeyEventMsg ke {};
    ke.subtype = 0; /* key event subtype */
    ke.down    = down;
    ke.keysym  = keysym;
    ke.keycode = keycode;
    return send_msg_to_server(proto::rfbQemuEvent, &ke, sizeof(ke));
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
        co_return error::make_error(ec);
    }
    co_await boost::asio::async_connect(socket_, endpoints, net_awaitable[ec]);
    if (ec) {
        spdlog::error("Failed to connect rfbserver [{}:{}] : {}", host_, port_, ec.message());
        co_return error::make_error(ec);
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
        co_return error::make_error(ec);
    }

    int major, minor;
    if (sscanf(pv, proto::rfbProtocolVersionFormat, &major, &minor) != 2) {
        spdlog::error("Not a valid VNC server ({})", pv);
        co_return error::make_error(
            boost::system::errc::make_error_code(boost::system::errc::wrong_protocol_type));
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
        co_return error::make_error(ec);
    }


    if (auto err = co_await async_authenticate(); err) {
        spdlog::error("Authentication with the server failed: {}", err.message());
        co_return err;
    }

    if (auto err = co_await async_client_init(); err) {
        spdlog::error("Failed to initialize the client: {}", err.message());
        co_return err;
    }

    co_return error {};
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
            co_return error::make_error(ec);

        std::vector<proto::rfbAuthScheme> tAuth(count);
        co_await boost::asio::async_read(socket_, boost::asio::buffer(tAuth), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
        if (handler_)
            selected_auth_scheme = handler_->select_auth_scheme({tAuth.begin(), tAuth.end()});

        co_await boost::asio::async_write(
            socket_, boost::asio::buffer(&selected_auth_scheme, 1), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);
    }
    else {
        boost::system::error_code ec;
        boost::endian::big_uint32_buf_t authScheme {};
        co_await boost::asio::async_read(
            socket_, boost::asio::buffer(&authScheme, sizeof(authScheme)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

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
                co_return error {};

        } break;
        case proto::rfbVncAuth: {
            boost::system::error_code ec;
            std::vector<uint8_t> challenge;
            challenge.resize(16);
            co_await boost::asio::async_read(
                socket_, boost::asio::buffer(challenge), net_awaitable[ec]);
            if (ec)
                co_return error::make_error(ec);

            co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
            if (handler_) {
                auto password = handler_->get_auth_password();
                detail::rfbEncryptBytes(challenge.data(), password.c_str());
            }

            co_await boost::asio::async_write(
                socket_, boost::asio::buffer(challenge), net_awaitable[ec]);
            if (ec)
                co_return error::make_error(ec);

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
    co_return error::make_error(
        custom_error::auth_error,
        fmt::format("Unimplemented authentication method: {}! ", (int)selected_auth_scheme));
}

boost::asio::awaitable<error> client_impl::async_client_init()
{
    boost::system::error_code ec;

    uint8_t shared = share_desktop_ ? 1 : 0;
    co_await boost::asio::async_write(
        socket_, boost::asio::buffer(&shared, sizeof(shared)), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    proto::rfbServerInitMsg si {};
    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&si, sizeof(si)), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    if (si.nameLength.value() > 1 << 20) {
        auto msg = fmt::format("Too big desktop name length sent by server: {} B > 1 MB",
                               (unsigned int)si.nameLength.value());
        spdlog::error(msg);
        co_return error::make_error(custom_error::client_init_error, msg);
    }

    std::string name;
    name.resize(si.nameLength.value());
    co_await boost::asio::async_read(socket_, boost::asio::buffer(name), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    spdlog::info("Desktop name \"{}\"", name);
    spdlog::info("Connected to VNC server, using protocol version {}.{}", major_, minor_);
    spdlog::info("VNC server default format:");
    si.format.print();

    frame_.init(si.framebufferWidth.value(), si.framebufferHeight.value(), si.format);

    desktop_name_ = std::move(name);
    co_return error {};
}

bool client_impl::send_format(const proto::rfbPixelFormat& format)
{
    proto::rfbSetPixelFormatMsg spf {};
    spf.pad1   = 0;
    spf.pad2   = 0;
    spf.format = format;

    if (send_msg_to_server(proto::rfbSetPixelFormat, &spf, sizeof(spf))) {
        frame_.set_format(format);
        return true;
    }
    return false;
}

bool client_impl::send_frame_encodings(const std::vector<std::string>& encodings)
{
    std::vector<boost::endian::big_uint32_buf_t> encs;

    bool requestCompressLevel    = false;
    bool requestQualityLevel     = false;
    bool requestLastRectEncoding = false;

    auto apply_codecs = codecs_ | std::views::filter([&](const auto& enc) {
                            if (!enc->is_frame_codec())
                                return true;

                            auto iter = std::ranges::find_if(encodings, [&](const auto& enc_name) {
                                return enc->codec_name() == enc_name;
                            });
                            return iter != encodings.end();
                        });

    for (const auto& codec : apply_codecs) {
        encs.emplace_back(codec->encoding_code());

        if (codec->requestCompressLevel())
            requestCompressLevel = true;
        if (codec->requestQualityLevel())
            requestQualityLevel = true;
        if (codec->requestLastRectEncoding())
            requestLastRectEncoding = true;
    }

    if (requestCompressLevel)
        encs.emplace_back(compress_level_ + proto::rfbEncodingCompressLevel0);

    if (requestQualityLevel)
        encs.emplace_back(quality_level_ + proto::rfbEncodingQualityLevel0);

    if (requestLastRectEncoding)
        encs.emplace_back(proto::rfbEncodingLastRect);

#ifdef LIBVNC_HAVE_LIBZ
    encs.emplace_back(proto::rfbEncodingExtendedClipboard);
#endif

    proto::rfbSetEncodingsMsg msg {};
    msg.pad        = 0;
    msg.nEncodings = encs.size();

    return send_msg_to_server_buffers(
        proto::rfbSetEncodings, boost::asio::buffer(&msg, sizeof(msg)), encs);
}

bool client_impl::send_scale_setting(int scale)
{
    proto::rfbSetScaleMsg ssm {};
    ssm.scale = scale;
    ssm.pad   = 0;

    if (supported_messages_.supports_client2server(proto::rfbSetScale)) {
        if (!send_msg_to_server(proto::rfbSetScale, &ssm, sizeof(ssm)))
            return false;
    }
    if (supported_messages_.supports_client2server(proto::rfbPalmVNCSetScaleFactor)) {
        if (!send_msg_to_server(proto::rfbPalmVNCSetScaleFactor, &ssm, sizeof(ssm)))
            return false;
    }
    return true;
}

bool client_impl::send_ext_desktop_size(const std::vector<proto::rfbExtDesktopScreen>& screens)
{
    if (screens.empty())
        return true;

    proto::rfbSetDesktopSizeMsg sdm {};
    sdm.pad1            = 0;
    sdm.width           = screens.front().width;
    sdm.height          = screens.front().height;
    sdm.numberOfScreens = screens.size();
    sdm.pad2            = 0;

    return send_msg_to_server_buffers(
        proto::rfbSetDesktopSize, boost::asio::buffer(&sdm, sizeof(sdm)), screens);
}

boost::asio::awaitable<error> client_impl::server_message_loop()
{
    boost::system::error_code ec;
    for (;;) {
        proto::rfbServerToClientMsg msg_id {};
        co_await boost::asio::async_read(
            socket_, boost::asio::buffer(&msg_id, sizeof(msg_id)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        auto iter = message_map_.find(msg_id);
        if (iter == message_map_.end()) {
            spdlog::error("Unknown message type {} from VNC server", (int)msg_id);
            co_return error::make_error(
                boost::system::errc::make_error_code(boost::system::errc::wrong_protocol_type));
        }
        auto err = co_await iter->second();
        if (!err)
            continue;

        co_return err;
    }
}

boost::asio::awaitable<error> client_impl::read_auth_result()
{
    boost::system::error_code ec;
    boost::endian::big_uint32_buf_t authResult {};
    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&authResult, sizeof(authResult)), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    switch (authResult.value()) {
        case proto::rfbVncAuthOK: {
            spdlog::info("VNC authentication succeeded");
            co_return error {};
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
    return send_msg_to_server_buffers(ID, boost::asio::buffer((const char*)data, len));
}

bool client_impl::send_msg_to_server_buffers(const proto::rfbClientToServerMsg& ID,
                                             const std::vector<boost::asio::const_buffer>& buffers)
{
    if (!supported_messages_.supports_client2server(ID)) {
        spdlog::warn("Unsupported client2server protocol: {}", (int)ID);
        return false;
    }
    std::vector<uint8_t> buffer(boost::asio::buffer_size(buffers) + sizeof(ID));
    std::memcpy(buffer.data(), &ID, sizeof(ID));
    boost::asio::buffer_copy(
        boost::asio::buffer(buffer.data() + sizeof(ID), buffer.size() - sizeof(ID)), buffers);

    send_raw_data(std::move(buffer));
    return true;
}

void client_impl::send_raw_data(std::vector<uint8_t>&& data)
{
    boost::asio::dispatch(strand_, [this, self = shared_from_this(), data = std::move(data)]() {
        if (!socket_.is_open())
            return;

        if (!is_initialization_completed_)
            return;

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

void client_impl::soft_cursor_lock_area(int x, int y, int w, int h)
{
}

void client_impl::got_cursor_shape(int xhot,
                                   int yhot,
                                   const frame_buffer& rc_source,
                                   const uint8_t* rc_mask)
{
}

void client_impl::handle_keyboard_led_state(int state)
{
    if (current_keyboard_led_state_ == state)
        return;

    current_keyboard_led_state_ = state;

    boost::asio::dispatch(strand_, [this, self = shared_from_this()]() {
        if (handler_)
            handler_->on_keyboard_led_state(current_keyboard_led_state_);
    });
}

void client_impl::handle_server_identity(std::string_view text)
{
    spdlog::info("Connected to Server \"{}\"", text);
}

void client_impl::handle_supported_messages(const proto::rfbSupportedMessages& messages)
{
    supported_messages_.assign(messages);
    supported_messages_.print();
}

void client_impl::handle_ext_desktop_screen(const std::vector<proto::rfbExtDesktopScreen>& screens)
{
    screens_ = screens;
    supported_messages_.set_client2server(proto::rfbSetDesktopSize);
}

void client_impl::resize_client_buffer(int width, int height)
{
    frame_.set_size(width, height);

    send_framebuffer_update_request(false);
    spdlog::info("Got new framebuffer size: {}x{}", width, height);
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbFramebufferUpdate()
{
    boost::system::error_code ec;
    proto::rfbFramebufferUpdateMsg msg {};
    proto::rfbFramebufferUpdateRectHeader UpdateRect {};

    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);


    for (int i = 0; i < msg.num_rects.value(); ++i) {
        co_await boost::asio::async_read(
            socket_, boost::asio::buffer(&UpdateRect, sizeof(UpdateRect)), net_awaitable[ec]);
        if (ec)
            co_return error::make_error(ec);

        auto encoding = (proto::rfbEncoding)UpdateRect.encoding.value();
        if (encoding == proto::rfbEncodingLastRect)
            break;

        auto iter = std::ranges::find_if(
            codecs_, [&](const auto& codec) { return codec->encoding_code() == encoding; });
        if (iter == codecs_.end()) {
            co_return error::make_error(custom_error::frame_error,
                                        fmt::format("Unsupported encoding: {}", (int)encoding));
        }
        const auto& codec = (*iter);

        auto err = co_await codec->decode(socket_, UpdateRect.r, frame_, shared_from_this());
        if (err)
            co_return err;
    }
    send_framebuffer_update_request(true);

    co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
    if (handler_)
        handler_->on_frame_update(frame_);

    co_return error {};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbSetColourMapEntries()
{
    co_return error {};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbBell()
{
    boost::system::error_code ec;
    co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
    if (handler_)
        handler_->on_bell();
    co_return error {};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbServerCutText()
{
    boost::system::error_code ec;
    proto::rfbServerCutTextMsg msg {};
    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    int32_t ilen     = msg.length.value();
    int32_t read_len = std::abs(ilen);

    boost::asio::streambuf buffer;
    co_await boost::asio::async_read(
        socket_, buffer, boost::asio::transfer_exactly(read_len), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    std::istream input_stream(&buffer);
    std::string text;
    if (ilen > 0) {
        text.resize(ilen);
        input_stream.read(text.data(), text.size());
        spdlog::info("Got server cut text: {}", text);
        co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
        if (handler_)
            handler_->on_cut_text(text);

        co_return error {};
    }

#if defined(LIBVNC_HAVE_LIBZ)
    boost::endian::big_uint32_buf_t flags {};
    input_stream.read((char*)&flags, sizeof(flags));

    /*
     * only process (text | provide). Ignore all others
     * modify here if need more types(rtf,html,dib,files)
     */
    if (!(flags.value() & proto::rfbExtendedClipboard_Text)) {
        spdlog::info("rfbServerCutTextMsg. not text type. ignore");
        co_return error {};
    }
    if (!(flags.value() & proto::rfbExtendedClipboard_Provide)) {
        spdlog::info("rfbServerCutTextMsg. not provide type. ignore");
        co_return error {};
    }
    if (flags.value() & proto::rfbExtendedClipboard_Caps) {
        spdlog::info("rfbServerCutTextMsg. default cap.");
        // client->extendedClipboardServerCapabilities |=
        //     rfbExtendedClipboard_Text; /* for now, only text */
        extendedClipboardServerCapabilities_.reset();
        extendedClipboardServerCapabilities_.set(proto::rfbExtendedClipboard_Text);
        co_return error {};
    }

    boost::endian::big_uint32_buf_t text_size {};
    zstr::istream zs(input_stream);
    if (!zs.read((char*)&text_size, sizeof(text_size))) {
        spdlog::error("rfbServerCutTextMsg. inflate size failed");
        co_return error {};
    }

    if (text_size.value() > (1 << 20)) {
        spdlog::error("rfbServerCutTextMsg. size too large");
        co_return error {};
    }
    text.resize(text_size.value());
    if (!zs.read(text.data(), text.size())) {
        spdlog::error("rfbServerCutTextMsg. inflate buf failed");
        co_return error {};
    }
    co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
    if (handler_)
        handler_->on_cut_text_utf8(text);

    spdlog::info("Got server cut text: {}", std::filesystem::u8path(text).string());
#endif
    co_return error {};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbTextChat()
{
    boost::system::error_code ec;
    proto::rfbTextChatMsg msg {};
    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    using namespace std::string_view_literals;

    switch (msg.length.value()) {
        case proto::rfbTextChatOpen: {
            spdlog::info("Received TextChat Open");
            co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
            if (handler_)
                handler_->on_text_chat(proto::rfbTextChatOpen, ""sv);
        } break;
        case proto::rfbTextChatClose: {
            spdlog::info("Received TextChat Close");
            co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
            if (handler_)
                handler_->on_text_chat(proto::rfbTextChatClose, ""sv);
        } break;
        case proto::rfbTextChatFinished: {
            spdlog::info("Received TextChat Finished");
            co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
            if (handler_)
                handler_->on_text_chat(proto::rfbTextChatFinished, ""sv);
        } break;
        default: {
            std::string message;
            message.resize(msg.length.value());
            co_await boost::asio::async_read(
                socket_, boost::asio::buffer(message), net_awaitable[ec]);
            if (ec)
                co_return error::make_error(ec);

            spdlog::info("Received TextChat \"{}\"", message);
            co_await boost::asio::dispatch(strand_, net_awaitable[ec]);
            if (handler_)
                handler_->on_text_chat(proto::rfbTextChatMessage, message);
        } break;
    }
    co_return error {};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbXvp()
{
    boost::system::error_code ec;
    proto::rfbXvpMsg msg {};
    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    supported_messages_.set_client2server(proto::rfbXvp);
    supported_messages_.set_server2client(proto::rfbXvp);
    co_return error {};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbResizeFrameBuffer()
{
    boost::system::error_code ec;
    proto::rfbResizeFrameBufferMsg msg {};
    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    resize_client_buffer(msg.framebufferWidth.value(), msg.framebufferHeight.value());
    co_return error {};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbPalmVNCReSizeFrameBuffer()
{
    boost::system::error_code ec;
    proto::rfbPalmVNCReSizeFrameBufferMsg msg {};
    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
    if (ec)
        co_return error::make_error(ec);

    resize_client_buffer(msg.buffer_w.value(), msg.buffer_h.value());
    co_return error {};
}

} // namespace libvnc