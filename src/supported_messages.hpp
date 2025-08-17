#pragma once
#include "libvnc-cpp/proto.h"
#include <bitset>
#include <shared_mutex>

namespace libvnc {
class supported_messages {
public:
	supported_messages();

public:
	void reset();
	void assign(const proto::rfbSupportedMessages &msg);

	void supported_ultra_vnc();
	void supported_tight_vnc();

	void set_client2server(int type, bool val = true);
	void set_server2client(int type, bool val = true);

	bool test_client2server(int type) const;
	bool test_server2client(int type) const;

	void print();

private:
	std::bitset<256> server2client_;
	std::bitset<256> client2server_;
	mutable std::shared_mutex mutex_;
};
} // namespace libvnc