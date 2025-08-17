#include "client_impl.h"

#include "d3des.hpp"
#include "dh.h"
#include "encoding/copy_rect.hpp"
#include "encoding/cursor.hpp"
#include "encoding/ext_desktop_size.hpp"
#include "encoding/hextile.hpp"
#include "encoding/keyboard_led_state.hpp"
#include "encoding/new_fb_size.hpp"
#include "encoding/pointer_pos.hpp"
#include "encoding/raw.hpp"
#include "encoding/rre.hpp"
#include "encoding/server_identity.hpp"
#include "encoding/supported_encodings.hpp"
#include "encoding/supported_messages.hpp"
#include "encoding/ultra.hpp"
#include "encoding/zlib.hpp"
#include "encoding/tight.hpp"
#include "libvnc-cpp/client.h"
#include "libvnc-cpp/error.h"
#include "libvnc-cpp/proto.h"
#include "use_awaitable.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <spdlog/spdlog.h>
#include <string.h>
#if defined(LIBVNC_HAVE_LIBZ)
#include <zstr.hpp>
#endif

namespace libvnc {

namespace detail {

unsigned char fixedkey[8] = {23, 82, 107, 6, 35, 78, 88, 7};

static void rfbEncryptBytes(uint8_t *challenge, const char *passwd)
{
	unsigned char key[8];
	unsigned int i;

	/* key is simply password padded with nulls */

	for (i = 0; i < 8; i++) {
		if (i < strlen(passwd)) {
			key[i] = passwd[i];
		} else {
			key[i] = 0;
		}
	}

	deskey(key, EN0);

	for (i = 0; i < 16; i += 8) {
		des(challenge + i, challenge + i);
	}
}
/*
 *   marscha@2006
 *   Encrypt bytes[length] in memory using key.
 *   Key has to be 8 bytes, length a multiple of 8 bytes.
 */
static void vncEncryptBytes2(unsigned char *where, const int length, unsigned char *key)
{
	int i, j;
	deskey(key, EN0);
	for (i = 0; i < 8; i++)
		where[i] ^= key[i];
	des(where, where);
	for (i = 8; i < length; i += 8) {
		for (j = 0; j < 8; j++)
			where[i + j] ^= where[i + j - 8];
		des(where + i, where + i);
	}
}

} // namespace detail

client_impl::client_impl(const boost::asio::any_io_executor &executor)
	: strand_(executor),
	  socket_(executor),
	  resolver_(executor)
{
	register_message(proto::rfbFramebufferUpdate, &client_impl::on_rfbFramebufferUpdate, this);
	register_message(proto::rfbSetColourMapEntries, &client_impl::on_rfbSetColourMapEntries, this);
	register_message(proto::rfbBell, &client_impl::on_rfbBell, this);
	register_message(proto::rfbServerCutText, &client_impl::on_rfbServerCutText, this);
	register_message(proto::rfbTextChat, &client_impl::on_rfbTextChat, this);
	register_message(proto::rfbXvp, &client_impl::on_rfbXvp, this);
	register_message(proto::rfbResizeFrameBuffer, &client_impl::on_rfbResizeFrameBuffer, this);
	register_message(proto::rfbPalmVNCReSizeFrameBuffer, &client_impl::on_rfbPalmVNCReSizeFrameBuffer, this);
	register_message(proto::rfbMonitorInfo, &client_impl::on_rfbMonitorInfo, this);
	register_message(proto::rfbKeepAlive, &client_impl::on_rfbKeepAlive, this);

	register_auth_message(proto::rfbNoAuth, &client_impl::on_rfbNoAuth, this);
	register_auth_message(proto::rfbVncAuth, &client_impl::on_rfbVncAuth, this);
	register_auth_message(proto::rfbUltraVNC, &client_impl::on_rfbUltraVNC, this);
	register_auth_message(proto::rfbUltraMSLogonII, &client_impl::on_rfbUltraMSLogonII, this);
	register_auth_message(proto::rfbClientInitExtraMsgSupport, &client_impl::on_rfbClientInitExtraMsgSupport, this);

	register_encoding<encoding::tight>();
	register_encoding<encoding::ultra>();
	register_encoding<encoding::zlib>();
	register_encoding<encoding::ultra_zip>();
	register_encoding<encoding::copy_rect>();
	register_encoding<encoding::co_rre>();
	register_encoding<encoding::rre>();
	register_encoding<encoding::hextile>();
	register_encoding<encoding::raw>();

	register_encoding<encoding::x_cursor>();
	register_encoding<encoding::rich_cursor>();
	register_encoding<encoding::keyboard_led_state>();
	register_encoding<encoding::new_fb_size>();
	register_encoding<encoding::pointer_pos>();
	register_encoding<encoding::server_identity>();
	register_encoding<encoding::supported_encodings>();
	register_encoding<encoding::ext_desktop_size>();
	register_encoding<encoding::supported_messages>();
}

const libvnc::frame_buffer &client_impl::frame() const
{
	return frame_;
}

void client_impl::close()
{
	if (status_ == client::status::closed)
		return;

	resolver_.cancel();
	if (socket_.is_open()) {
		boost::system::error_code ec;
		socket_.shutdown(boost::asio::socket_base::shutdown_both, ec);
		socket_.close(ec);
	}
	desktop_name_.clear();
	current_keyboard_led_state_ = 0;
	extendedClipboardServerCapabilities_.reset();
	nbrMonitors_ = 0;
	ultra_server_ = false;
	brfbClientInitExtraMsgSupportNew_ = false;

	commit_status(client::status::closed);
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
	if (status_ != client::status::closed)
		co_return error::make_error(
			boost::system::errc::make_error_code(boost::system::errc::connection_already_in_progress));

	commit_status(client::status::connecting);
	if (error err = co_await async_connect_rfbserver(); err) {
		close();
		handler_.on_connect(err);
		co_return err;
	}

	commit_status(client::status::handshaking);
	if (error err = co_await async_handshake(); err) {
		close();
		handler_.on_connect(err);
		co_return err;
	}

	commit_status(client::status::authenticating);
	if (auto err = co_await async_authenticate(); err) {
		close();
		handler_.on_connect(err);
		spdlog::error("Authentication with the server failed: {}", err.message());
		co_return err;
	}

	commit_status(client::status::initializing);
	if (auto err = co_await async_client_init(); err) {
		close();
		handler_.on_connect(err);
		spdlog::error("Failed to initialize the client: {}", err.message());
		co_return err;
	}
	commit_status(client::status::connected);
	handler_.on_connect(error{});

	for (const auto &codec : codecs_)
		codec->init();

	send_framebuffer_update_request(false);

	boost::system::error_code ec;
	auto remote_endp = socket_.remote_endpoint(ec);

	using namespace boost::asio::experimental::awaitable_operators;
	auto run_result = co_await(server_message_loop() || server_keepalive_loop());
	error err = std::visit([](auto &&arg) { return arg; }, run_result);

	spdlog::warn("Disconnect from the rbfserver [{}:{}] : {}", remote_endp.address().to_string(),
		     remote_endp.port(), err.message());

	close();
	handler_.on_disconnect(err);
	co_return error{};
}

void client_impl::send_framebuffer_update_request(int x, int y, int w, int h, bool incremental)
{
	proto::rfbFramebufferUpdateRequestMsg msg{};
	msg.x = x;
	msg.y = y;
	msg.w = w;
	msg.h = h;
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
	for (const auto &item : codecs_) {
		if (!item->is_frame_codec())
			continue;
		encs.push_back(item->codec_name());
	}
	return encs;
}

bool client_impl::send_pointer_event(int x, int y, int buttonMask)
{
	proto::rfbPointerEventMsg pe{};
	pe.buttonMask = buttonMask;
	pe.x = std::max(x, 0);
	pe.y = std::max(y, 0);
	return send_msg_to_server(proto::rfbPointerEvent, &pe, sizeof(pe));
}

bool client_impl::send_client_cut_text(std::string_view text)
{
	proto::rfbClientCutTextMsg cct{};
	cct.pad1 = 0;
	cct.pad2 = 0;
	cct.length = (int)text.size();

	return send_msg_to_server_buffers(proto::rfbClientCutText, boost::asio::buffer(&cct, sizeof(cct)), text);
}

bool client_impl::send_client_cut_text_utf8(std::string_view text)
{
	if (!extendedClipboardServerCapabilities_.any())
		return false;
#if defined(LIBVNC_HAVE_LIBZ)
	boost::endian::big_uint32_buf_t flags{};
	flags = proto::rfbExtendedClipboard_Provide | proto::rfbExtendedClipboard_Text;

	boost::asio::streambuf compress_buffer;
	{
		boost::endian::big_uint32_buf_t text_size{};
		text_size = (uint32_t)text.size();

		zstr::ostream z_os(&compress_buffer, zstr::default_buff_size, -1, 15);
		z_os.write((char *)&text_size, sizeof(text_size));
		z_os.write(text.data(), text.size());
	}
	int len = static_cast<int>(sizeof(flags) + compress_buffer.size());

	proto::rfbClientCutTextMsg cct{};
	cct.pad1 = 0;
	cct.pad2 = 0;
	cct.length = -len;

	return send_msg_to_server_buffers(proto::rfbClientCutText, boost::asio::buffer(&cct, sizeof(cct)),
					  boost::asio::buffer(&flags, sizeof(flags)), compress_buffer.data());
#else
	return false;
#endif
}

bool client_impl::text_chat_send(std::string_view text)
{
	proto::rfbTextChatMsg chat{};
	chat.pad1 = 0;
	chat.pad2 = 0;
	chat.length = (int)text.length();
	return send_msg_to_server_buffers(proto::rfbTextChat, boost::asio::buffer(&chat, sizeof(chat)), text);
}

bool client_impl::text_chat_open()
{
	proto::rfbTextChatMsg chat{};
	chat.pad1 = 0;
	chat.pad2 = 0;
	chat.length = proto::rfbTextChatOpen;
	return send_msg_to_server(proto::rfbTextChat, &chat, sizeof(chat));
}

bool client_impl::text_chat_close()
{
	proto::rfbTextChatMsg chat{};
	chat.pad1 = 0;
	chat.pad2 = 0;
	chat.length = proto::rfbTextChatClose;
	return send_msg_to_server(proto::rfbTextChat, &chat, sizeof(chat));
}

bool client_impl::text_chat_finish()
{
	proto::rfbTextChatMsg chat{};
	chat.pad1 = 0;
	chat.pad2 = 0;
	chat.length = proto::rfbTextChatFinished;
	return send_msg_to_server(proto::rfbTextChat, &chat, sizeof(chat));
}

bool client_impl::permit_server_input(bool enabled)
{
	proto::rfbSetServerInputMsg msg{};
	msg.pad = 0;
	msg.status = enabled ? 1 : 0;
	return send_msg_to_server(proto::rfbSetServerInput, &msg, sizeof(msg));
}

bool client_impl::send_xvp_msg(uint8_t version, proto::rfbXvpCode code)
{
	proto::rfbXvpMsg xvp{};
	xvp.pad = 0;
	xvp.version = version;
	xvp.code = code;
	return send_msg_to_server(proto::rfbXvp, &xvp, sizeof(xvp));
}

bool client_impl::send_set_monitor(uint8_t nbr)
{
	proto::rfbMonitorMsg mm{};
	mm.pad2 = 0;
	mm.pad3 = 0;
	mm.nbr = nbr;
	if (nbr <= nbrMonitors_) {
		return send_msg_to_server(proto::rfbSetMonitor, &mm, sizeof(mm));
	}
	return false;
}

bool client_impl::send_key_event(uint32_t key, bool down)
{
	proto::rfbKeyEventMsg ke{};
	ke.down = down ? 1 : 0;
	ke.key = key;
	return send_msg_to_server(proto::rfbKeyEvent, &ke, sizeof(ke));
}

bool client_impl::send_extended_key_event(uint32_t keysym, uint32_t keycode, bool down)
{
	proto::rfbQemuExtendedKeyEventMsg ke{};
	ke.subtype = 0; /* key event subtype */
	ke.down = down;
	ke.keysym = keysym;
	ke.keycode = keycode;
	return send_msg_to_server(proto::rfbQemuEvent, &ke, sizeof(ke));
}

boost::asio::awaitable<error> client_impl::async_connect_rfbserver()
{
	// connect
	boost::system::error_code ec;
	auto endpoints = co_await resolver_.async_resolve(host_, std::to_string(port_), net_awaitable[ec]);
	if (ec) {
		spdlog::error("Failed to resolve [{}:{}] : {}", host_, port_, ec.message());
		co_return error::make_error(ec);
	}
	co_await boost::asio::async_connect(socket_, endpoints, net_awaitable[ec]);
	if (ec) {
		spdlog::error("Failed to connect rfbserver [{}:{}] : {}", host_, port_, ec.message());
		co_return error::make_error(ec);
	}
	co_return error{};
}

boost::asio::awaitable<libvnc::error> client_impl::async_handshake()
{
	boost::system::error_code ec;

	// handshake
	proto::rfbProtocolVersionMsg pv = {0};
	co_await boost::asio::async_read(socket_, boost::asio::buffer(&pv, sizeof(pv)),
					 boost::asio::transfer_exactly(proto::sz_rfbProtocolVersionMsg),
					 net_awaitable[ec]);
	if (ec) {
		spdlog::error("Failed to read rfbserver handshake data [{}:{}] : {}", host_, port_, ec.message());
		co_return error::make_error(ec);
	}

	int major, minor;
	if (sscanf_s(pv, proto::rfbProtocolVersionFormat, &major, &minor) != 2) {
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
		spdlog::info("UltraVNC Single Click server detected, enabling UltraVNC specific messages: {}", pv);
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
	spdlog::info("VNC server supports protocol version {}.{} (viewer {}.{})", major, minor,
		     proto::rfbProtocolMajorVersion, proto::rfbProtocolMinorVersion);

	sprintf_s(pv, proto::rfbProtocolVersionFormat, major_, minor_);
	co_await boost::asio::async_write(socket_, boost::asio::buffer(&pv, proto::sz_rfbProtocolVersionMsg),
					  net_awaitable[ec]);

	if (ec) {
		spdlog::error("Failed to send rfbserver handshake data [{}:{}] : {}", host_, port_, ec.message());
		co_return error::make_error(ec);
	}
	co_return error{};
}

boost::asio::awaitable<error> client_impl::async_authenticate()
{
	proto::rfbAuthScheme selected_auth_scheme = proto::rfbConnFailed;

	/* 3.7 and onwards sends a # of security types first */
	if (major_ == 3 && minor_ > 6) {
		boost::system::error_code ec;

		uint8_t count = 0;
		co_await boost::asio::async_read(socket_, boost::asio::buffer(&count, sizeof(count)),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		std::vector<proto::rfbAuthScheme> tAuth(count);
		co_await boost::asio::async_read(socket_, boost::asio::buffer(tAuth), net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		std::erase_if(tAuth, [this](const proto::rfbAuthScheme &auth_scheme) {
			if (auth_scheme == proto::rfbClientInitExtraMsgSupportNew) {
				brfbClientInitExtraMsgSupportNew_ = true;
				return true;
			}
			return false;
		});

		selected_auth_scheme = select_auth_scheme({tAuth.begin(), tAuth.end()});

		co_await boost::asio::async_write(socket_, boost::asio::buffer(&selected_auth_scheme, 1),
						  net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);
	} else {
		boost::system::error_code ec;
		boost::endian::big_uint32_buf_t authScheme{};
		co_await boost::asio::async_read(socket_, boost::asio::buffer(&authScheme, sizeof(authScheme)),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		selected_auth_scheme = static_cast<proto::rfbAuthScheme>(authScheme.value());
	}

	auto iter = auth_message_map_.find(selected_auth_scheme);
	if (iter == auth_message_map_.end()) {
		co_return error::make_error(custom_error::auth_error,
					    fmt::format("Unimplemented authentication method: {}! ",
							(int)selected_auth_scheme));
	}
	co_return co_await iter->second();
}

boost::asio::awaitable<error> client_impl::async_client_init()
{
	boost::system::error_code ec;

	proto::rfbClientInitMsg ci{};
	uint8_t flags = proto::clientInitNotShare;
	if (share_desktop_)
		flags |= proto::clientInitShared;
	if (brfbClientInitExtraMsgSupportNew_)
		flags |= proto::clientInitExtraMsgSupport;
	ci.flags = flags;

	co_await boost::asio::async_write(socket_, boost::asio::buffer(&ci, sizeof(ci)), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	if (brfbClientInitExtraMsgSupportNew_) {
		brfbClientInitExtraMsgSupportNew_ = false;
		if (auto err = co_await async_send_client_init_extra_msg(); err)
			co_return err;
	}

	proto::rfbServerInitMsg si{};
	co_await boost::asio::async_read(socket_, boost::asio::buffer(&si, sizeof(si)), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	if (si.nameLength.value() > 1 << 20) {
		auto msg = fmt::format("Too big desktop name length sent by server: {} B > 1 MB",
				       (unsigned int)si.nameLength.value());
		spdlog::error(msg);
		co_return error::make_error(custom_error::client_init_error, msg);
	}

	desktop_name_.resize(si.nameLength.value());
	co_await boost::asio::async_read(socket_, boost::asio::buffer(desktop_name_), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	spdlog::info("Desktop name \"{}\"", desktop_name_);
	spdlog::info("Connected to VNC server, using protocol version {}.{}", major_, minor_);
	spdlog::info("VNC server default format:");
	si.format.print();

	int width = si.framebufferWidth.value();
	int height = si.framebufferHeight.value();

	handler_.on_new_frame_size(width, height);
	if (auto format = handler_.want_format(); format && send_format(*format)) {
		frame_.init(width, height, *format);
	} else {
		frame_.init(width, height, si.format);
	}
	send_frame_encodings(supported_frame_encodings());

	co_return error{};
}

boost::asio::awaitable<error> client_impl::async_send_client_init_extra_msg()
{
	boost::system::error_code ec;
	proto::rfbClientInitExtraMsg msg{};
	msg.textLength = notifiction_text_.length();

	co_await boost::asio::async_write(socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	if (!notifiction_text_.empty()) {
		co_await boost::asio::async_write(socket_, boost::asio::buffer(notifiction_text_), net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);
	}
	co_return error{};
}

bool client_impl::send_format(const proto::rfbPixelFormat &format)
{
	proto::rfbSetPixelFormatMsg spf{};
	spf.pad1 = 0;
	spf.pad2 = 0;
	spf.format = format;
	return send_msg_to_server(proto::rfbSetPixelFormat, &spf, sizeof(spf));
}

bool client_impl::send_frame_encodings(const std::vector<std::string> &encodings)
{
	std::vector<boost::endian::big_uint32_buf_t> encs;

	bool requestCompressLevel = false;
	bool requestQualityLevel = false;
	bool requestLastRectEncoding = false;

	auto apply_codecs = codecs_ | std::views::filter([&](const auto &enc) {
				    if (!enc->is_frame_codec())
					    return true;

				    auto iter = std::ranges::find_if(encodings, [&](const auto &enc_name) {
					    return enc->codec_name() == enc_name;
				    });
				    return iter != encodings.end();
			    });

	for (const auto &codec : apply_codecs) {
		encs.emplace_back(codec->encoding_code());

		if (codec->request_compress_level())
			requestCompressLevel = true;
		if (codec->request_quality_level())
			requestQualityLevel = true;
		if (codec->request_last_rect_encoding())
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
	encs.emplace_back(proto::rfbEncodingMonitorInfo);
	encs.emplace_back(proto::rfbEncodingEnableKeepAlive);

	proto::rfbSetEncodingsMsg msg{};
	msg.pad = 0;
	msg.nEncodings = static_cast<uint16_t>(encs.size());

	return send_msg_to_server_buffers(proto::rfbSetEncodings, boost::asio::buffer(&msg, sizeof(msg)), encs);
}

bool client_impl::send_scale_setting(int scale)
{
	proto::rfbSetScaleMsg ssm{};
	ssm.scale = scale;
	ssm.pad = 0;

	if (supported_messages_.test_client2server(proto::rfbSetScale)) {
		if (!send_msg_to_server(proto::rfbSetScale, &ssm, sizeof(ssm)))
			return false;
	}
	if (supported_messages_.test_client2server(proto::rfbPalmVNCSetScaleFactor)) {
		if (!send_msg_to_server(proto::rfbPalmVNCSetScaleFactor, &ssm, sizeof(ssm)))
			return false;
	}
	return true;
}

bool client_impl::send_ext_desktop_size(const std::vector<proto::rfbExtDesktopScreen> &screens)
{
	if (screens.empty())
		return true;

	proto::rfbSetDesktopSizeMsg sdm{};
	sdm.pad1 = 0;
	sdm.width = screens.front().width;
	sdm.height = screens.front().height;
	sdm.numberOfScreens = static_cast<uint8_t>(screens.size());
	sdm.pad2 = 0;

	return send_msg_to_server_buffers(proto::rfbSetDesktopSize, boost::asio::buffer(&sdm, sizeof(sdm)), screens);
}

boost::asio::awaitable<error> client_impl::server_message_loop()
{
	boost::system::error_code ec;
	for (;;) {
		proto::rfbServerToClientMsg msg_id{};
		co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg_id, sizeof(msg_id)),
						 net_awaitable[ec]);
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

boost::asio::awaitable<error> client_impl::server_keepalive_loop()
{
	boost::system::error_code ec;
	boost::asio::steady_timer timer(co_await boost::asio::this_coro::executor);
	for (;;) {
		using namespace std::chrono_literals;
		timer.expires_after(1s);
		co_await timer.async_wait(net_awaitable[ec]);

		if (ec)
			co_return error::make_error(ec);

		if (supported_messages_.test_client2server(proto::rfbKeepAlive))
			send_msg_to_server_buffers(proto::rfbKeepAlive);
	}
	co_return error{};
}

boost::asio::awaitable<error> client_impl::read_auth_result()
{
	boost::system::error_code ec;
	boost::endian::big_uint32_buf_t authResult{};
	co_await boost::asio::async_read(socket_, boost::asio::buffer(&authResult, sizeof(authResult)),
					 net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	switch (authResult.value()) {
	case proto::rfbVncAuthOK: {
		spdlog::info("VNC authentication succeeded");
		co_return error{};
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
	case proto::rfbVncAuthContinue: {
		co_return co_await async_authenticate();
	} break;
	default:
		break;
	}
	auto msg = std::format("Unknown VNC authentication result: {}", authResult.value());
	spdlog::error(msg);
	co_return error::make_error(custom_error::auth_error, msg);
}

boost::asio::awaitable<error> client_impl::read_error_reason()
{
	boost::system::error_code ec;
	boost::endian::big_uint32_buf_t reasonLen{};
	co_await boost::asio::async_read(socket_, boost::asio::buffer(&reasonLen, sizeof(reasonLen)),
					 net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	if (reasonLen.value() > 1 << 20) {
		auto msg = fmt::format("VNC connection failed, but sent reason length of {} exceeds limit of 1MB",
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

bool client_impl::send_msg_to_server(const proto::rfbClientToServerMsg &ID, const void *data, std::size_t len)
{
	return send_msg_to_server_buffers(ID, boost::asio::buffer((const char *)data, len));
}

bool client_impl::send_msg_to_server_buffers(const proto::rfbClientToServerMsg &ID,
					     const std::vector<boost::asio::const_buffer> &buffers)
{
	if (!supported_messages_.test_client2server(ID)) {
		spdlog::warn("Unsupported client2server protocol: {}", (int)ID);
		return false;
	}
	std::vector<uint8_t> buffer(boost::asio::buffer_size(buffers) + sizeof(ID));
	std::memcpy(buffer.data(), &ID, sizeof(ID));
	boost::asio::buffer_copy(boost::asio::buffer(buffer.data() + sizeof(ID), buffer.size() - sizeof(ID)), buffers);

	send_raw_data(std::move(buffer));
	return true;
}

void client_impl::send_raw_data(std::vector<uint8_t> &&data)
{
	boost::asio::dispatch(strand_, [this, self = shared_from_this(), data = std::move(data)]() {
		if (!socket_.is_open())
			return;

		bool write_in_proccess = !send_que_.empty();
		send_que_.push_back(std::move(data));
		if (write_in_proccess)
			return;

		boost::asio::co_spawn(
			strand_,
			[this, self = shared_from_this()]() -> boost::asio::awaitable<void> {
				while (!send_que_.empty()) {
					const auto &buffer = send_que_.front();

					boost::system::error_code ec;
					co_await boost::asio::async_write(socket_, boost::asio::buffer(buffer),
									  net_awaitable[ec]);
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

void client_impl::commit_status(const client::status &s)
{
	if (status_ == s)
		return;
	status_ = s;
	handler_.on_status_changed(s);
}

proto::rfbAuthScheme client_impl::select_auth_scheme(const std::set<proto::rfbAuthScheme> &auths)
{
	if (auths.count(proto::rfbUltraVNC))
		return proto::rfbUltraVNC;

	if (auths.count(proto::rfbClientInitExtraMsgSupport))
		return proto::rfbClientInitExtraMsgSupport;

	if (auto result = handler_.select_auth_scheme(auths); result)
		return result.value();

	return proto::rfbConnFailed;
}

void client_impl::soft_cursor_lock_area(int x, int y, int w, int h) {}

void client_impl::got_cursor_shape(int xhot, int yhot, const frame_buffer &rc_source, const uint8_t *rc_mask)
{
	handler_.on_cursor_shape(xhot, yhot, rc_source, rc_mask);
}

void client_impl::handle_cursor_pos(int x, int y)
{
	handler_.on_cursor_pos(x, y);
}

void client_impl::handle_keyboard_led_state(int state)
{
	if (current_keyboard_led_state_ == state)
		return;

	current_keyboard_led_state_ = state;
	handler_.on_keyboard_led_state(state);
}

void client_impl::handle_server_identity(std::string_view text)
{
	spdlog::info("Connected to Server \"{}\"", text);
}

void client_impl::handle_supported_messages(const proto::rfbSupportedMessages &messages)
{
	supported_messages_.assign(messages);
	supported_messages_.print();
}

void client_impl::handle_ext_desktop_screen(const std::vector<proto::rfbExtDesktopScreen> &screens)
{
	screens_ = screens;
	supported_messages_.set_client2server(proto::rfbSetDesktopSize);
}

void client_impl::handle_resize_client_buffer(int width, int height)
{
	handler_.on_new_frame_size(width, height);
	frame_.set_size(width, height);

	send_framebuffer_update_request(false);
	spdlog::info("Got new framebuffer size: {}x{}", width, height);
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbNoAuth()
{
	spdlog::info("No authentication needed");

	/* 3.8 and upwards sends a Security Result for rfbNoAuth */
	if ((major_ == 3 && minor_ > 7) || major_ > 3)
		co_return co_await read_auth_result();
	else
		co_return error{};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbVncAuth()
{
	boost::system::error_code ec;
	std::vector<uint8_t> challenge;
	challenge.resize(16);
	co_await boost::asio::async_read(socket_, boost::asio::buffer(challenge), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	if (auto password = handler_.get_auth_password(); password)
		detail::rfbEncryptBytes(challenge.data(), password->c_str());

	co_await boost::asio::async_write(socket_, boost::asio::buffer(challenge), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	co_return co_await read_auth_result();
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbUltraVNC()
{
	supported_messages_.supported_ultra_vnc();
	ultra_server_ = true;
	co_return co_await read_auth_result();
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbUltraMSLogonII()
{
	try {

		boost::endian::big_int64_buf_t gen;
		boost::endian::big_int64_buf_t mod;
		boost::endian::big_int64_buf_t resp;

		co_await boost::asio::async_read(socket_, boost::asio::buffer(&gen, sizeof(gen)));
		co_await boost::asio::async_read(socket_, boost::asio::buffer(&mod, sizeof(mod)));
		co_await boost::asio::async_read(socket_, boost::asio::buffer(&resp, sizeof(resp)));

		boost::endian::big_int64_buf_t pub;
		DH dh(gen.value(), mod.value());
		pub = dh.createInterKey();

		co_await boost::asio::async_write(socket_, boost::asio::buffer(&pub, sizeof(pub)));

		boost::endian::big_int64_buf_t key;
		key = dh.createEncryptionKey(resp.value());

		spdlog::info("After DH: g={}, m={}, i={}, key={}", gen.value(), mod.value(), pub.value(), key.value());

		char user[256] = {0};
		char passwd[64] = {0};

		if (auto result = handler_.get_auth_ms_account(); result) {
			strncpy_s(passwd, result->second.c_str(), 64);
			strncpy_s(user, result->first.c_str(), 254);
		}

		detail::vncEncryptBytes2((unsigned char *)user, sizeof(user), key.data());
		detail::vncEncryptBytes2((unsigned char *)passwd, sizeof(passwd), key.data());

		co_await boost::asio::async_write(socket_, boost::asio::buffer(&user, sizeof(user)));
		co_await boost::asio::async_write(socket_, boost::asio::buffer(&passwd, sizeof(passwd)));

		co_return co_await read_auth_result();

	} catch (const boost::system::system_error &e) {
		co_return error::make_error(e.code());
	}
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbClientInitExtraMsgSupport()
{
	if (auto err = co_await async_send_client_init_extra_msg(); err)
		co_return err;
	co_return co_await read_auth_result();
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbFramebufferUpdate()
{
	boost::system::error_code ec;
	proto::rfbFramebufferUpdateMsg msg{};
	proto::rfbFramebufferUpdateRectHeader UpdateRect{};

	co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	for (int i = 0; i < msg.num_rects.value(); ++i) {
		co_await boost::asio::async_read(socket_, boost::asio::buffer(&UpdateRect, sizeof(UpdateRect)),
						 net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		auto encoding = (proto::rfbEncoding)UpdateRect.encoding.value();
		if (encoding == proto::rfbEncodingLastRect)
			break;

		auto iter = std::ranges::find_if(codecs_,
						 [&](const auto &codec) { return codec->encoding_code() == encoding; });
		if (iter == codecs_.end()) {
			co_return error::make_error(custom_error::frame_error,
						    fmt::format("Unsupported encoding: {}", (int)encoding));
		}
		const auto &codec = (*iter);

		auto err = co_await codec->decode(socket_, UpdateRect.r, frame_, shared_from_this());
		if (err)
			co_return err;
	}
	send_framebuffer_update_request(true);
	handler_.on_frame_update(frame_);

	co_return error{};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbSetColourMapEntries()
{
	co_return error{};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbBell()
{
	handler_.on_bell();
	co_return error{};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbServerCutText()
{
	boost::system::error_code ec;
	proto::rfbServerCutTextMsg msg{};
	co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	int32_t ilen = msg.length.value();
	int32_t read_len = std::abs(ilen);

	boost::asio::streambuf buffer;
	co_await boost::asio::async_read(socket_, buffer, boost::asio::transfer_exactly(read_len), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	std::istream input_stream(&buffer);
	std::string text;
	if (ilen > 0) {
		text.resize(ilen);
		input_stream.read(text.data(), text.size());
		spdlog::info("Got server cut text: {}", text);

		handler_.on_cut_text(text);
		co_return error{};
	}

#if defined(LIBVNC_HAVE_LIBZ)
	boost::endian::big_uint32_buf_t flags{};
	input_stream.read((char *)&flags, sizeof(flags));

	/*
     * only process (text | provide). Ignore all others
     * modify here if need more types(rtf,html,dib,files)
     */
	if (!(flags.value() & proto::rfbExtendedClipboard_Text)) {
		spdlog::info("rfbServerCutTextMsg. not text type. ignore");
		co_return error{};
	}
	if (!(flags.value() & proto::rfbExtendedClipboard_Provide)) {
		spdlog::info("rfbServerCutTextMsg. not provide type. ignore");
		co_return error{};
	}
	if (flags.value() & proto::rfbExtendedClipboard_Caps) {
		spdlog::info("rfbServerCutTextMsg. default cap.");
		// client->extendedClipboardServerCapabilities |=
		//     rfbExtendedClipboard_Text; /* for now, only text */
		extendedClipboardServerCapabilities_.reset();
		extendedClipboardServerCapabilities_ |= proto::rfbExtendedClipboard_Text;
		co_return error{};
	}

	boost::endian::big_uint32_buf_t text_size{};
	zstr::istream zs(input_stream, zstr::default_buff_size, false, 15);
	if (!zs.read((char *)&text_size, sizeof(text_size))) {
		spdlog::error("rfbServerCutTextMsg. inflate size failed");
		co_return error{};
	}

	if (text_size.value() > (1 << 20)) {
		spdlog::error("rfbServerCutTextMsg. size too large");
		co_return error{};
	}
	text.resize(text_size.value());
	if (!zs.read(text.data(), text.size())) {
		spdlog::error("rfbServerCutTextMsg. inflate buf failed");
		co_return error{};
	}

	spdlog::info("Got server cut text: {}", std::filesystem::u8path(text).string());
	handler_.on_cut_text_utf8(text);
#endif
	co_return error{};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbTextChat()
{
	boost::system::error_code ec;
	proto::rfbTextChatMsg msg{};
	co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	using namespace std::string_view_literals;

	switch (msg.length.value()) {
	case proto::rfbTextChatOpen: {
		spdlog::info("Received TextChat Open");
		handler_.on_text_chat(proto::rfbTextChatOpen, ""sv);
	} break;
	case proto::rfbTextChatClose: {
		spdlog::info("Received TextChat Close");
		handler_.on_text_chat(proto::rfbTextChatClose, ""sv);
	} break;
	case proto::rfbTextChatFinished: {
		spdlog::info("Received TextChat Finished");
		handler_.on_text_chat(proto::rfbTextChatFinished, ""sv);
	} break;
	default: {
		std::string message;
		message.resize(msg.length.value());
		co_await boost::asio::async_read(socket_, boost::asio::buffer(message), net_awaitable[ec]);
		if (ec)
			co_return error::make_error(ec);

		spdlog::info("Received TextChat \"{}\"", message);
		handler_.on_text_chat(proto::rfbTextChatMessage, message);
	} break;
	}
	co_return error{};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbXvp()
{
	boost::system::error_code ec;
	proto::rfbXvpMsg msg{};
	co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	supported_messages_.set_client2server(proto::rfbXvp);
	co_return error{};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbResizeFrameBuffer()
{
	boost::system::error_code ec;
	proto::rfbResizeFrameBufferMsg msg{};
	co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	handle_resize_client_buffer(msg.framebufferWidth.value(), msg.framebufferHeight.value());
	co_return error{};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbPalmVNCReSizeFrameBuffer()
{
	boost::system::error_code ec;
	proto::rfbPalmVNCReSizeFrameBufferMsg msg{};
	co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	handle_resize_client_buffer(msg.buffer_w.value(), msg.buffer_h.value());
	co_return error{};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbMonitorInfo()
{
	boost::system::error_code ec;
	proto::rfbMonitorMsg msg{};

	co_await boost::asio::async_read(socket_, boost::asio::buffer(&msg, sizeof(msg)), net_awaitable[ec]);
	if (ec)
		co_return error::make_error(ec);

	nbrMonitors_ = msg.nbr.value();
	supported_messages_.set_client2server(proto::rfbSetMonitor);
	handler_.on_monitor_info(nbrMonitors_);
	co_return error{};
}

boost::asio::awaitable<libvnc::error> client_impl::on_rfbKeepAlive()
{
	if (!supported_messages_.test_client2server(proto::rfbKeepAlive))
		supported_messages_.set_client2server(proto::rfbKeepAlive);
	co_return error{};
}

} // namespace libvnc