#include "client_impl.h"

#include "use_awaitable.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <openssl/dh.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <spdlog/spdlog.h>
#include <string.h>
#if (OPENSSL_VERSION_NUMBER >= 0x30000000L)
#include <openssl/provider.h>
#endif

#include "libvnc-cpp/client.h"
#include "proto.h"

namespace libvnc {

namespace detail {

static unsigned char reverseByte(unsigned char b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static int encrypt_rfbdes(unsigned char* out,
                          int* out_len,
                          const unsigned char key[8],
                          const unsigned char* in,
                          const size_t in_len)
{
    int result          = 0;
    EVP_CIPHER_CTX* des = NULL;
    unsigned char mungedkey[8];
    int i;
#if (OPENSSL_VERSION_NUMBER >= 0x30000000L)
    OSSL_PROVIDER* providerLegacy  = NULL;
    OSSL_PROVIDER* providerDefault = NULL;
#endif

    for (i = 0; i < 8; i++)
        mungedkey[i] = reverseByte(key[i]);

#if (OPENSSL_VERSION_NUMBER >= 0x30000000L)
    /* Load Multiple providers into the default (NULL) library context */
    if (!(providerLegacy = OSSL_PROVIDER_load(NULL, "legacy")))
        goto out;
    if (!(providerDefault = OSSL_PROVIDER_load(NULL, "default")))
        goto out;
#endif

    if (!(des = EVP_CIPHER_CTX_new()))
        goto out;
    if (!EVP_EncryptInit_ex(des, EVP_des_ecb(), NULL, mungedkey, NULL))
        goto out;
    if (!EVP_EncryptUpdate(des, out, out_len, in, in_len))
        goto out;

    result = 1;

out:
    if (des)
        EVP_CIPHER_CTX_free(des);
#if (OPENSSL_VERSION_NUMBER >= 0x30000000L)
    if (providerLegacy)
        OSSL_PROVIDER_unload(providerLegacy);
    if (providerDefault)
        OSSL_PROVIDER_unload(providerDefault);
#endif
    return result;
}

static void rfbEncryptBytes(std::vector<uint8_t>& bytes, std::string_view passwd)
{
    unsigned char key[8];
    unsigned int i;
    int out_len;

    /* key is simply password padded with nulls */

    for (i = 0; i < 8; i++) {
        if (i < passwd.size()) {
            key[i] = passwd[i];
        }
        else {
            key[i] = 0;
        }
    }

    encrypt_rfbdes(bytes.data(), &out_len, key, bytes.data(), bytes.size());
}

} // namespace detail

client_impl::client_impl(client* self,
                         boost::asio::io_context& executor,
                         std::string_view host,
                         uint16_t port)
    : self_(self)
    , executor_(executor)
    , socket_(executor)
    , resolver_(executor)
    , host_(host)
    , port_(port)
{
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
        executor_,
        [this]() -> boost::asio::awaitable<void> {
            try {
                co_await connect();
                co_await handshake();
                co_await authenticate();
                co_await client_init();
                co_await server_message_loop();
            }
            catch (const boost::system::system_error& e) {
            }
        },
        boost::asio::detached);
}

void client_impl::sendFramebufferUpdateRequest(int x, int y, int w, int h, bool incremental)
{
    proto::rfbFramebufferUpdateRequestMsg msg {};
    msg.x           = x;
    msg.y           = y;
    msg.w           = w;
    msg.h           = h;
    msg.incremental = incremental;
    send_data(proto::rfbFramebufferUpdateRequest, &msg, sizeof(msg));
}

boost::asio::awaitable<void> client_impl::connect()
{
    close();
    auto endpoints = co_await resolver_.async_resolve(host_, std::to_string(port_));
    co_await boost::asio::async_connect(socket_, endpoints);
}

boost::asio::awaitable<void> client_impl::handshake()
{
    rfbProtocolVersionMsg pv = {0};
    co_await boost::asio::async_read(
        socket_, boost::asio::buffer(&pv, sizeof(pv)), boost::asio::transfer_exactly(12));


    if (sscanf(pv, rfbProtocolVersionFormat, &major_, &minor_) != 2) {
        spdlog::error("Not a valid VNC server ({})", pv);
        throw boost::system::system_error(
            boost::system::errc::make_error_code(boost::system::errc::wrong_protocol_type));
    }
    spdlog::info("Connected to VNC server, using protocol version {}.{}", major_, minor_);

    /* fall back to viewer supported version */
    if ((major_ == rfbProtocolMajorVersion) && (minor_ > rfbProtocolMinorVersion))
        minor_ = rfbProtocolMinorVersion;

    sprintf(pv, rfbProtocolVersionFormat, major_, minor_);
    co_await boost::asio::async_write(socket_, boost::asio::buffer(&pv, sz_rfbProtocolVersionMsg));
}

boost::asio::awaitable<void> client_impl::authenticate()
{
    client::auth_scheme_type selected_auth_scheme = client::auth_scheme_type::rfbConnFailed;

    /* 3.7 and onwards sends a # of security types first */
    if (major_ == 3 && minor_ > 6) {
        uint8_t count = 0;
        co_await boost::asio::async_read(socket_, boost::asio::buffer(&count, sizeof(count)));

        std::vector<client::auth_scheme_type> tAuth(count);
        co_await boost::asio::async_read(socket_, boost::asio::buffer(tAuth));

        selected_auth_scheme = self_->select_auth_scheme(tAuth);
        co_await boost::asio::async_write(socket_, boost::asio::buffer(&selected_auth_scheme, 1));
    }
    else {
        uint32_t authScheme = 0;
        co_await boost::asio::async_read(socket_,
                                         boost::asio::buffer(&authScheme, sizeof(authScheme)));

        authScheme           = boost::asio::detail::socket_ops::network_to_host_long(authScheme);
        selected_auth_scheme = static_cast<client::auth_scheme_type>(authScheme);
    }

    switch (selected_auth_scheme) {
        case client::auth_scheme_type::rfbConnFailed: break;
        case client::auth_scheme_type::rfbNoAuth: {
            spdlog::info("No authentication needed");

            /* 3.8 and upwards sends a Security Result for rfbNoAuth */
            if ((major_ == 3 && minor_ > 7) || major_ > 3)
                co_await read_auth_result();

        } break;
        case client::auth_scheme_type::rfbVncAuth: {
            if (!get_password_handler_) {
                throw;
            }
            std::vector<uint8_t> challenge;
            challenge.resize(16);
            co_await boost::asio::async_read(socket_, boost::asio::buffer(challenge));

            auto password = get_password_handler_();
            detail::rfbEncryptBytes(challenge, password);

            co_await boost::asio::async_write(socket_, boost::asio::buffer(challenge));

            co_await read_auth_result();

        } break;
        case client::auth_scheme_type::rfbRA2: break;
        case client::auth_scheme_type::rfbRA2ne: break;
        case client::auth_scheme_type::rfbSSPI: break;
        case client::auth_scheme_type::rfbSSPIne: break;
        case client::auth_scheme_type::rfbTight: break;
        case client::auth_scheme_type::rfbUltra: break;
        case client::auth_scheme_type::rfbTLS: break;
        case client::auth_scheme_type::rfbVeNCrypt: break;
        case client::auth_scheme_type::rfbSASL: break;
        case client::auth_scheme_type::rfbARD: break;
        case client::auth_scheme_type::rfbUltraMSLogonI: break;
        case client::auth_scheme_type::rfbUltraMSLogonII: break;
        case client::auth_scheme_type::rfbUltraVNC_SecureVNCPluginAuth: break;
        case client::auth_scheme_type::rfbUltraVNC_SecureVNCPluginAuth_new: break;
        case client::auth_scheme_type::rfbClientInitExtraMsgSupport: break;
        case client::auth_scheme_type::rfbClientInitExtraMsgSupportNew: break;
        default: break;
    }
}

boost::asio::awaitable<void> client_impl::client_init()
{
    uint8_t shared = app_data_.shareDesktop ? 1 : 0;
    co_await boost::asio::async_write(socket_, boost::asio::buffer(&shared, 1));

    co_await boost::asio::async_read(socket_, boost::asio::buffer(&si, sizeof(si)));

    si.framebufferWidth =
        boost::asio::detail::socket_ops::network_to_host_short(si.framebufferWidth);
    si.framebufferHeight =
        boost::asio::detail::socket_ops::network_to_host_short(si.framebufferHeight);
    si.format.redMax   = boost::asio::detail::socket_ops::network_to_host_short(si.format.redMax);
    si.format.greenMax = boost::asio::detail::socket_ops::network_to_host_short(si.format.greenMax);
    si.format.blueMax  = boost::asio::detail::socket_ops::network_to_host_short(si.format.blueMax);
    si.nameLength      = boost::asio::detail::socket_ops::network_to_host_long(si.nameLength);

    if (si.nameLength > 1 << 20) {
        spdlog::error("Too big desktop name length sent by server: {} B > 1 MB",
                      (unsigned int)si.nameLength);
    }
    this->desktopName_.resize(si.nameLength);

    co_await boost::asio::async_read(socket_, boost::asio::buffer(this->desktopName_));
    spdlog::info("Desktop name \"{}\"", this->desktopName_);

    spdlog::info("Connected to VNC server, using protocol version {}.{}", major_, minor_);

    spdlog::info("VNC server default format:");
    si.format.print();
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
            case proto::rfbBell: break;
            case proto::rfbServerCutText: break;
            case proto::rfbResizeFrameBuffer: break;
            case proto::rfbPalmVNCReSizeFrameBuffer: break;
            case proto::rfbServerState: break;
            default: break;
        }
    }
}

boost::asio::awaitable<void> client_impl::read_auth_result()
{
    boost::endian::big_uint32_buf_t authResult {};
    co_await boost::asio::async_read(socket_, boost::asio::buffer(&authResult, sizeof(authResult)));

    switch (authResult.value()) {
        case proto::rfbVncAuthOK: {
            spdlog::info("VNC authentication succeeded");
            co_return;
        } break;
        case proto::rfbVncAuthFailed: {
            if (major_ == 3 && minor_ > 7) {
                /* we have an error following */
                co_await read_error_reason();
            }
        } break;
        case proto::rfbVncAuthTooMany: {
            spdlog::error("VNC authentication failed - too many tries");
        } break;
        default: break;
    }
    spdlog::error("Unknown VNC authentication result: {}", authResult.value());
}

boost::asio::awaitable<std::string> client_impl::read_error_reason()
{
    boost::endian::big_uint32_buf_t reasonLen {};
    co_await boost::asio::async_read(socket_, boost::asio::buffer(&reasonLen, sizeof(reasonLen)));

    if (reasonLen.value() > 1 << 20) {
        auto msg =
            fmt::format("VNC connection failed, but sent reason length of {} exceeds limit of 1MB",
                        reasonLen.value());
        spdlog::error(msg);
        co_return msg;
    }

    std::string reason;
    reason.resize(reasonLen.value());

    co_await boost::asio::async_read(socket_, boost::asio::buffer(reason));

    spdlog::error("VNC connection failed: {}", reason);

    co_return reason;
}

void client_impl::send_data(const proto::rfbClientToServerMsg& ID,
                            const void* data,
                            std::size_t len)
{
    std::vector<uint8_t> buffer;
    buffer.reserve(len + sizeof(ID));
    buffer.push_back(ID);
    std::copy((uint8_t*)data, (uint8_t*)data + len, std::back_inserter(buffer));
    send_raw_data(std::move(buffer));
}

void client_impl::send_raw_data(const std::span<uint8_t>& data)
{
    send_raw_data(std::vector<uint8_t>(data.begin(), data.end()));
}

void client_impl::send_raw_data(std::vector<uint8_t>&& data)
{
    boost::asio::dispatch(executor_, [this, data = std::move(data)]() {
        bool write_in_proccess = !send_que_.empty();
        send_que_.push_back(std::move(data));
        if (write_in_proccess)
            return;

        boost::asio::co_spawn(
            executor_,
            [this]() -> boost::asio::awaitable<void> {
                while (!send_que_.empty()) {
                    const auto& buffer = send_que_.front();

                    boost::system::error_code ec;
                    boost::asio::async_write(
                        socket_, boost::asio::buffer(buffer), net_awaitable[ec]);
                    if (ec)
                        co_return;

                    send_que_.pop_front();
                }
            },
            boost::asio::detached);
    });
}

boost::asio::awaitable<void> client_impl::on_message(const proto::rfbFramebufferUpdateMsg& msg)
{
    proto::rfbFramebufferUpdateRectHeader UpdateRect {};
    for (int i = 0; i < msg.num_rects.value(); ++i) {
        co_await boost::asio::async_read(socket_,
                                         boost::asio::buffer(&UpdateRect, sizeof(UpdateRect)));

        switch (UpdateRect.encoding.value()) {
            case proto::rfbEncodingLastRect: co_return; break;
            case proto::rfbEncodingXCursor: {
                auto bytesPerPixel = si.format.bitsPerPixel / 8;
                auto bytesPerRow   = (UpdateRect.r.w.value() + 7) / 8;
                auto bytesMaskData = bytesPerRow * UpdateRect.r.h.value();

                rfbXCursorColors XCursorColors {};
                co_await boost::asio::async_read(
                    socket_, boost::asio::buffer(&XCursorColors, sizeof(XCursorColors)));

                std::vector<uint8_t> maskDataBuffer;
                maskDataBuffer.resize(bytesMaskData);
                co_await boost::asio::async_read(socket_, boost::asio::buffer(maskDataBuffer));

            } break;
            case proto::rfbEncodingRichCursor: {
                auto bytesPerPixel = si.format.bitsPerPixel / 8;

                std::vector<uint8_t> buffer;
                buffer.resize(UpdateRect.r.w.value() * UpdateRect.r.h.value() * bytesPerPixel);
                co_await boost::asio::async_read(socket_, boost::asio::buffer(buffer));

            } break;
            case proto::rfbEncodingKeyboardLedState: {
            } break;
            case proto::rfbEncodingNewFBSize: {
                sendFramebufferUpdateRequest(
                    0, 0, UpdateRect.r.w.value(), UpdateRect.r.h.value(), false);
            } break;

            default: break;
        }
    }
}

void client_impl::SetClient2Server(int messageType)
{
}

} // namespace libvnc