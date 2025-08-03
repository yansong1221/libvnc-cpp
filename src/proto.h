#pragma once
#include <boost/asio/awaitable.hpp>
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/endian/buffers.hpp>
#include <cstdint>

namespace libvnc::proto {


/*****************************************************************************
 *
 * Encoding types
 *
 *****************************************************************************/
enum rfbEncoding : uint32_t
{
    rfbEncodingRaw      = 0,
    rfbEncodingCopyRect = 1,
    rfbEncodingRRE      = 2,
    rfbEncodingCoRRE    = 4,
    rfbEncodingHextile  = 5,
    rfbEncodingZlib     = 6,
    rfbEncodingTight    = 7,
    rfbEncodingTightPng = 0xFFFFFEFC, // -260
    rfbEncodingZlibHex  = 8,
    rfbEncodingUltra    = 9,
    rfbEncodingTRLE     = 15,
    rfbEncodingZRLE     = 16,
    rfbEncodingZYWRLE   = 17,

    rfbEncodingH264 = 0x48323634,

    // Cache & XOR-Zlib - rdv@2002
    rfbEncodingCache              = 0xFFFF0000,
    rfbEncodingCacheEnable        = 0xFFFF0001,
    rfbEncodingXOR_Zlib           = 0xFFFF0002,
    rfbEncodingXORMonoColor_Zlib  = 0xFFFF0003,
    rfbEncodingXORMultiColor_Zlib = 0xFFFF0004,
    rfbEncodingSolidColor         = 0xFFFF0005,
    rfbEncodingXOREnable          = 0xFFFF0006,
    rfbEncodingCacheZip           = 0xFFFF0007,
    rfbEncodingSolMonoZip         = 0xFFFF0008,
    rfbEncodingUltraZip           = 0xFFFF0009,

    // Xvp pseudo-encoding
    rfbEncodingXvp = 0xFFFFFECB,

    // Special encoding numbers
    rfbEncodingFineQualityLevel0   = 0xFFFFFE00,
    rfbEncodingFineQualityLevel100 = 0xFFFFFE64,
    rfbEncodingSubsamp1X           = 0xFFFFFD00,
    rfbEncodingSubsamp4X           = 0xFFFFFD01,
    rfbEncodingSubsamp2X           = 0xFFFFFD02,
    rfbEncodingSubsampGray         = 0xFFFFFD03,
    rfbEncodingSubsamp8X           = 0xFFFFFD04,
    rfbEncodingSubsamp16X          = 0xFFFFFD05,

    rfbEncodingCompressLevel0 = 0xFFFFFF00,
    rfbEncodingCompressLevel1 = 0xFFFFFF01,
    rfbEncodingCompressLevel2 = 0xFFFFFF02,
    rfbEncodingCompressLevel3 = 0xFFFFFF03,
    rfbEncodingCompressLevel4 = 0xFFFFFF04,
    rfbEncodingCompressLevel5 = 0xFFFFFF05,
    rfbEncodingCompressLevel6 = 0xFFFFFF06,
    rfbEncodingCompressLevel7 = 0xFFFFFF07,
    rfbEncodingCompressLevel8 = 0xFFFFFF08,
    rfbEncodingCompressLevel9 = 0xFFFFFF09,

    rfbEncodingXCursor    = 0xFFFFFF10,
    rfbEncodingRichCursor = 0xFFFFFF11,
    rfbEncodingPointerPos = 0xFFFFFF18,

    rfbEncodingLastRect       = 0xFFFFFF20,
    rfbEncodingNewFBSize      = 0xFFFFFF21,
    rfbEncodingExtDesktopSize = 0xFFFFFECC,

    rfbEncodingQualityLevel0 = 0xFFFFFFE0,
    rfbEncodingQualityLevel1 = 0xFFFFFFE1,
    rfbEncodingQualityLevel2 = 0xFFFFFFE2,
    rfbEncodingQualityLevel3 = 0xFFFFFFE3,
    rfbEncodingQualityLevel4 = 0xFFFFFFE4,
    rfbEncodingQualityLevel5 = 0xFFFFFFE5,
    rfbEncodingQualityLevel6 = 0xFFFFFFE6,
    rfbEncodingQualityLevel7 = 0xFFFFFFE7,
    rfbEncodingQualityLevel8 = 0xFFFFFFE8,
    rfbEncodingQualityLevel9 = 0xFFFFFFE9,

    rfbEncodingQemuExtendedKeyEvent = 0xFFFFFEFE, // -258
    rfbEncodingExtendedClipboard    = 0xC0A1E5CE,
    // adzm 2010-09 - Notify streaming DSM plugin support
    rfbEncodingPluginStreaming = 0xC0A1E5CF,

    // LibVNCServer additions
    rfbEncodingKeyboardLedState   = 0xFFFE0000,
    rfbEncodingSupportedMessages  = 0xFFFE0001,
    rfbEncodingSupportedEncodings = 0xFFFE0002,
    rfbEncodingServerIdentity     = 0xFFFE0003,
};

enum rfbServerToClientMsg : uint8_t
{
    rfbFramebufferUpdate   = 0,
    rfbSetColourMapEntries = 1,
    rfbBell                = 2,
    rfbServerCutText       = 3,
    /* Modif sf@2002 */
    rfbResizeFrameBuffer        = 4,
    rfbPalmVNCReSizeFrameBuffer = 0xF,
    rfbServerState              = 0xAD

};

enum rfbClientToServerMsg : uint8_t
{
    rfbSetPixelFormat           = 0,
    rfbFixColourMapEntries      = 1, /* not currently supported */
    rfbSetEncodings             = 2,
    rfbFramebufferUpdateRequest = 3,
    rfbKeyEvent                 = 4,
    rfbPointerEvent             = 5,
    rfbClientCutText            = 6,
    rfbFileTransfer             = 7,    // Modif sf@2002 - actually bidirectionnal
    rfbSetScale                 = 8,    // Modif sf@2002
    rfbSetServerInput           = 9,    // Modif rdv@2002
    rfbSetSW                    = 10,   // Modif rdv@2002
    rfbTextChat                 = 11,   // Modif sf@2002 - Text Chat - Bidirectionnal
    rfbKeepAlive                = 13,   // 16 July 2008 jdp -- bidirectional
    rfbPalmVNCSetScaleFactor    = 0xF,  // PalmVNC 1.4 & 2.0 SetScale Factor message
    rfbNotifyPluginStreaming    = 0x50, // adzm 2010-09 - Notify streaming DSM plugin support

    rfbRequestSession = 20,
    rfbSetSession     = 21,
    rfbSetDesktopSize = 251,
    rfbMonitorInfo    = 252,
    rfbSetMonitor     = 254
};

enum rfbVncAuthResult : uint32_t
{
    rfbVncAuthOK     = 0,
    rfbVncAuthFailed = 1,

    // neither of these are used any longer in RFB 3.8
    rfbVncAuthTooMany = 2,

    // adzm 2010-05-11 - Send an explanatory message for the failure (if any)
    rfbVncAuthFailedEx = 3,

    // adzm 2010-09 - rfbUltraVNC or other auths may send this to restart authentication
    // (perhaps over a now-secure channel)
    rfbVncAuthContinue = 0xFFFFFFFF
};

struct rfbFramebufferUpdateMsg
{
    boost::endian::big_uint8_buf_t padding;
    boost::endian::big_uint16_buf_t num_rects;
};

struct rfbRectangle
{
    boost::endian::big_uint16_buf_t x;
    boost::endian::big_uint16_buf_t y;
    boost::endian::big_uint16_buf_t w;
    boost::endian::big_uint16_buf_t h;
};

/*
 * Each rectangle of pixel data consists of a header describing the position
 * and size of the rectangle and a type word describing the encoding of the
 * pixel data, followed finally by the pixel data. Note that if the client has
 * not sent a SetEncodings message then it will only receive raw pixel data.
 * Also note again that this structure is a multiple of 4 bytes.
 */

struct rfbFramebufferUpdateRectHeader
{
    rfbRectangle r;
    boost::endian::big_uint32_buf_t encoding; /* one of the encoding types rfbEncoding... */
};

struct rfbXCursorColors
{
    boost::endian::big_uint8_buf_t foreRed;
    boost::endian::big_uint8_buf_t foreGreen;
    boost::endian::big_uint8_buf_t foreBlue;
    boost::endian::big_uint8_buf_t backRed;
    boost::endian::big_uint8_buf_t backGreen;
    boost::endian::big_uint8_buf_t backBlue;
};

struct rfbFramebufferUpdateRequestMsg
{
    boost::endian::big_uint8_buf_t incremental;
    boost::endian::big_uint16_buf_t x;
    boost::endian::big_uint16_buf_t y;
    boost::endian::big_uint16_buf_t w;
    boost::endian::big_uint16_buf_t h;
};

} // namespace libvnc::proto