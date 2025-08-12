#pragma once
#include <boost/asio/awaitable.hpp>
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/endian/buffers.hpp>
#include <cstdint>

namespace libvnc::proto {

constexpr auto rfbProtocolVersionFormat = "RFB %03d.%03d\n";
constexpr auto rfbProtocolMajorVersion  = 3;
constexpr auto rfbProtocolMinorVersion  = 8;

typedef char rfbProtocolVersionMsg[13]; /* allow extra byte for null */
constexpr auto sz_rfbProtocolVersionMsg = 12;

/** Note that the CoRRE encoding uses this buffer and assumes it is big enough
to hold 255 * 255 * 32 bits -> 260100 bytes.  640*480 = 307200 bytes.
Hextile also assumes it is big enough to hold 16 * 16 * 32 bits.
Tight encoding assumes BUFFER_SIZE is at least 16384 bytes. */
constexpr auto RFB_BUFFER_SIZE = 640 * 480;

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
    rfbSetMonitor     = 254,
    /* Xvp message - bidirectional */
    rfbXvp = 250
};

enum rfbAuthScheme : uint8_t
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

enum rfbTextChatType : int32_t
{
    rfbTextChatMessage  = 0,
    rfbTextChatOpen     = -1,
    rfbTextChatClose    = -2,
    rfbTextChatFinished = -3,
};

struct rfbPixelFormat
{
    boost::endian::big_uint8_buf_t bitsPerPixel = {}; /* 8,16,32 only */

    boost::endian::big_uint8_buf_t depth = {}; /* 8 to 32 */

    boost::endian::big_uint8_buf_t bigEndian = {}; /* True if multi-byte pixels are interpreted
                        as big endian, or if single-bit-per-pixel
                        has most significant bit of the byte
                        corresponding to first (leftmost) pixel. Of
                        course this is meaningless for 8-bit/pix */

    boost::endian::big_uint8_buf_t trueColour = {}; /* If false then we need a "colour map" to
                         convert pixels to RGB. If true, xxxMax and
                         xxxShift specify bits used for red, green
                         and blue */

    /* the following fields are only meaningful if trueColour is true */

    boost::endian::big_uint16_buf_t redMax = {}; /* maximum red value (= 2^n - 1 where n is the
                      number of bits used for red). Note this
                      value is always in big endian order. */

    boost::endian::big_uint16_buf_t greenMax = {}; /* similar for green */

    boost::endian::big_uint16_buf_t blueMax = {}; /* and blue */

    boost::endian::big_uint8_buf_t redShift = {}; /* number of shifts needed to get the red
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

    boost::endian::big_uint8_buf_t greenShift = {}; /* similar for green */

    boost::endian::big_uint8_buf_t blueShift = {}; /* and blue */

    boost::endian::big_uint8_buf_t pad1  = {};
    boost::endian::big_uint16_buf_t pad2 = {};

    rfbPixelFormat() = default;
    rfbPixelFormat(int bitsPerSample, int samplesPerPixel, int bytesPerPixel);
    void print() const;
};

struct rfbSetPixelFormatMsg
{
    boost::endian::big_uint8_buf_t pad1 {};
    boost::endian::big_uint16_buf_t pad2 {};
    rfbPixelFormat format;
};

struct rfbServerInitMsg
{
    boost::endian::big_uint16_buf_t framebufferWidth;
    boost::endian::big_uint16_buf_t framebufferHeight;
    rfbPixelFormat format; /* the server's preferred pixel format */
    boost::endian::big_uint32_buf_t nameLength;
    /* followed by char name[nameLength] */
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

struct rfbExtDesktopSizeMsg
{
    boost::endian::big_uint8_buf_t numberOfScreens;
    boost::endian::big_uint8_buf_t pad[3];

    /* Followed by rfbExtDesktopScreen[numberOfScreens] */
};

struct rfbExtDesktopScreen
{
    boost::endian::big_uint32_buf_t id;
    boost::endian::big_uint16_buf_t x;
    boost::endian::big_uint16_buf_t y;
    boost::endian::big_uint16_buf_t width;
    boost::endian::big_uint16_buf_t height;
    boost::endian::big_uint32_buf_t flags;
};
struct rfbSupportedMessages
{
    uint8_t client2server[32]; /* maximum of 256 message types (256/8)=32 */
    uint8_t server2client[32]; /* maximum of 256 message types (256/8)=32 */
};

struct rfbCopyRect
{
    boost::endian::big_uint16_buf_t srcX;
    boost::endian::big_uint16_buf_t srcY;
};

/*-----------------------------------------------------------------------------
 * SetEncodings - tell the RFB server which encoding types we accept. Put them
 * in order of preference, if we have any. We may always receive raw
 * encoding, even if we don't specify it here.
 */

struct rfbSetEncodingsMsg
{
    boost::endian::big_uint8_buf_t pad;
    boost::endian::big_uint16_buf_t nEncodings;
    /* followed by nEncodings * CARD32 encoding types */
};

/*-----------------------------------------------------------------------------
 * ServerCutText - the server has new text in its cut buffer.
 */

enum rfbExtendedClipboard
{
    rfbExtendedClipboard_Text    = 1,
    rfbExtendedClipboard_RTF     = 2,
    rfbExtendedClipboard_HTML    = 4,
    rfbExtendedClipboard_DIB     = 8,
    rfbExtendedClipboard_Files   = 16,
    rfbExtendedClipboard_Caps    = (1 << 24),
    rfbExtendedClipboard_Request = (1 << 25),
    rfbExtendedClipboard_Peek    = (1 << 26),
    rfbExtendedClipboard_Notify  = (1 << 27),
    rfbExtendedClipboard_Provide = (1 << 28),
};

struct rfbServerCutTextMsg
{
    boost::endian::big_uint8_buf_t pad1;
    boost::endian::big_uint16_buf_t pad2;
    boost::endian::big_int32_buf_t length;
    /* followed by char text[length] */
};


struct rfbTextChatMsg
{
    boost::endian::big_uint8_buf_t pad1;  /*  Could be used later as an additionnal param */
    boost::endian::big_uint16_buf_t pad2; /*  Could be used later as text offset, for instance */
    boost::endian::big_int32_buf_t
        length; /*  Specific values for Open, close, finished (-1, -2, -3) */
    /* followed by char text[length] */
};

struct rfbXvpMsg
{
    boost::endian::big_uint8_buf_t pad;
    boost::endian::big_uint8_buf_t version; /* xvp extension version */
    boost::endian::big_uint8_buf_t code;    /* xvp message code */
};


struct rfbResizeFrameBufferMsg
{
    boost::endian::big_uint8_buf_t pad1;
    boost::endian::big_uint16_buf_t framebufferWidth;  /*  FrameBuffer width */
    boost::endian::big_uint16_buf_t framebufferHeight; /*  FrameBuffer height */
};

struct rfbPalmVNCReSizeFrameBufferMsg
{
    boost::endian::big_uint8_buf_t pad1;
    boost::endian::big_uint16_buf_t desktop_w; /* Desktop width */
    boost::endian::big_uint16_buf_t desktop_h; /* Desktop height */
    boost::endian::big_uint16_buf_t buffer_w;  /* FrameBuffer width */
    boost::endian::big_uint16_buf_t buffer_h;  /* Framebuffer height */
    boost::endian::big_uint16_buf_t pad2;
};

enum rfbButtonMask : uint8_t
{
    rfbButton1Mask   = 1,
    rfbButton2Mask   = 2,
    rfbButton3Mask   = 4,
    rfbButton4Mask   = 8,
    rfbButton5Mask   = 16,
    rfbWheelUpMask   = rfbButton4Mask, // RealVNC 335 method
    rfbWheelDownMask = rfbButton5Mask
};

struct rfbPointerEventMsg
{
    boost::endian::big_uint8_buf_t buttonMask; /* bits 0-7 are buttons 1-8, 0=up, 1=down */
    boost::endian::big_uint16_buf_t x;
    boost::endian::big_uint16_buf_t y;
};

struct rfbKeyEventMsg
{
    boost::endian::big_uint8_buf_t down; /* true if down (press), false if up */
    boost::endian::big_uint16_buf_t pad;
    boost::endian::big_uint32_buf_t key; /* key is specified as an X keysym */
};

} // namespace libvnc::proto