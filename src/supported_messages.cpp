#include "supported_messages.hpp"
#include <spdlog/spdlog.h>

namespace libvnc {

supported_messages::supported_messages()
{
	reset();
}

void supported_messages::reset()
{
	std::unique_lock<std::shared_mutex> lck(mutex_);

	server2client_.reset();
	client2server_.reset();

	/* Default client supported messages (universal RFB 3.3 protocol) */
	client2server_.set(proto::rfbSetPixelFormat);
	/* Not currently supported */
	client2server_.set(proto::rfbSetEncodings);
	client2server_.set(proto::rfbFramebufferUpdateRequest);
	client2server_.set(proto::rfbKeyEvent);
	client2server_.set(proto::rfbPointerEvent);
	client2server_.set(proto::rfbClientCutText);

	/* technically, we only care what we can *send* to the server
     * but, we set Server2Client Just in case it ever becomes useful
     */
	server2client_.set(proto::rfbFramebufferUpdate);
	server2client_.set(proto::rfbSetColourMapEntries);
	server2client_.set(proto::rfbBell);
	server2client_.set(proto::rfbServerCutText);
}

void supported_messages::supported_ultra_vnc()
{
	reset();

	std::unique_lock<std::shared_mutex> lck(mutex_);
	client2server_.set(proto::rfbFileTransfer);
	client2server_.set(proto::rfbSetScale);
	client2server_.set(proto::rfbSetServerInput);
	client2server_.set(proto::rfbSetSW);
	client2server_.set(proto::rfbTextChat);
	client2server_.set(proto::rfbPalmVNCSetScaleFactor);
	/* technically, we only care what we can *send* to the server */
	server2client_.set(proto::rfbResizeFrameBuffer);
	server2client_.set(proto::rfbPalmVNCReSizeFrameBuffer);
	server2client_.set(proto::rfbFileTransfer);
	server2client_.set(proto::rfbTextChat);
}

void supported_messages::supported_tight_vnc()
{
	reset();

	std::unique_lock<std::shared_mutex> lck(mutex_);
	client2server_.set(proto::rfbFileTransfer);
	client2server_.set(proto::rfbSetServerInput);
	client2server_.set(proto::rfbSetSW);
	/* technically, we only care what we can *send* to the server */
	server2client_.set(proto::rfbFileTransfer);
	server2client_.set(proto::rfbTextChat);
}

void supported_messages::set_client2server(int type, bool val /*= true*/)
{
	std::unique_lock<std::shared_mutex> lck(mutex_);
	client2server_.set(type, val);
}

void supported_messages::set_server2client(int type, bool val /*= true*/)
{
	std::unique_lock<std::shared_mutex> lck(mutex_);
	server2client_.set(type, val);
}

bool supported_messages::test_client2server(int type) const
{
	std::shared_lock<std::shared_mutex> lck(mutex_);
	return client2server_.test(type);
}

bool supported_messages::test_server2client(int type) const
{
	std::shared_lock<std::shared_mutex> lck(mutex_);
	return server2client_.test(type);
}

void supported_messages::assign(const proto::rfbSupportedMessages &msg)
{
	std::unique_lock<std::shared_mutex> lck(mutex_);
	server2client_.reset();
	client2server_.reset();

	for (int i = 0; i < 32; i++) {
		uint8_t client2server_byte = msg.client2server[i];
		uint8_t server2client_byte = msg.server2client[i];
		for (int j = 0; j < 8; ++j) {
			client2server_[i * 8 + j] = (client2server_byte >> (7 - j)) & 1;
			server2client_[i * 8 + j] = (server2client_byte >> (7 - j)) & 1;
		}
	}
}

void supported_messages::print()
{
	std::shared_lock<std::shared_mutex> lck(mutex_);
	spdlog::info("client2server supported messages (bit flags): {}", client2server_.to_string());
	spdlog::info("server2client supported messages (bit flags): {}", server2client_.to_string());
}

} // namespace libvnc