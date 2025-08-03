#pragma once
#include <boost/asio/any_io_executor.hpp>
#include <memory>

namespace libvnc {
class client_impl;
class client
{
public:
    enum class auth_scheme_type : uint8_t
    {
        rfbConnFailed    = 0,
        rfbNoAuth        = 1,
        rfbVncAuth       = 2,
        rfbRA2           = 5,
        rfbRA2ne         = 6,
        rfbSSPI          = 7,
        rfbSSPIne        = 8,
        rfbTight         = 16,
        rfbUltra         = 17,
        rfbTLS           = 18,
        rfbVeNCrypt      = 19,
        rfbSASL          = 20,
        rfbARD           = 30,
        rfbUltraMSLogonI = 0x70, /* UNIMPLEMENTED */

        // MS-Logon I never seems to be used anymore -- the old code would say if (m_ms_logon)
        // AuthMsLogon (II) else AuthVnc
        // and within AuthVnc would be if (m_ms_logon) { /* MS-Logon code */ }. That could never be
        // hit since the first case would always match!
        rfbUltraMSLogonII = 0x71,

        // Handshake needed to change for a possible security leak
        // Only new viewers can connect
        rfbUltraVNC_SecureVNCPluginAuth     = 0x72,
        rfbUltraVNC_SecureVNCPluginAuth_new = 0x73,
        rfbClientInitExtraMsgSupport        = 0x74,
        rfbClientInitExtraMsgSupportNew     = 0x75
    };

    using connect_handler_type      = std::function<void(const boost::system::error_code&)>;
    using get_password_handler_type = std::function<std::string()>;

public:
    client(boost::asio::io_context& executor,
           std::string_view host,
           uint16_t port = 5900);
    virtual ~client();

public:
    void start();

protected:
    virtual auth_scheme_type select_auth_scheme(const std::vector<auth_scheme_type>& auths);


private:
    friend class client_impl;
    std::unique_ptr<client_impl> impl_;
};
} // namespace libvnc