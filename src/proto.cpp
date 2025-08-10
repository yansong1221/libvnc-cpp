#include "libvnc-cpp/proto.h"
#include <spdlog/spdlog.h>

namespace libvnc::proto {


namespace detail {
static bool is_big_endian()
{
    static int endianTest = 1;
    return *(char*)&endianTest ? false : true;
}
} // namespace detail

rfbPixelFormat::rfbPixelFormat(int bitsPerSample, int samplesPerPixel, int bytesPerPixel)
{
    bitsPerPixel = bytesPerPixel * 8;
    depth        = bitsPerSample * samplesPerPixel;
    bigEndian    = detail::is_big_endian();
    trueColour   = 1;

    if (bitsPerPixel.value() == 8) {
        redMax     = 7;
        greenMax   = 7;
        blueMax    = 3;
        redShift   = 0;
        greenShift = 3;
        blueShift  = 6;
    }
    else {
        redMax   = (1 << bitsPerSample) - 1;
        greenMax = (1 << bitsPerSample) - 1;
        blueMax  = (1 << bitsPerSample) - 1;
        if (!bigEndian.value()) {
            redShift   = 0;
            greenShift = bitsPerSample;
            blueShift  = bitsPerSample * 2;
        }
        else {
            if (bitsPerPixel.value() == 8 * 3) {
                redShift   = bitsPerSample * 2;
                greenShift = bitsPerSample * 1;
                blueShift  = 0;
            }
            else {
                redShift   = bitsPerSample * 3;
                greenShift = bitsPerSample * 2;
                blueShift  = bitsPerSample;
            }
        }
    }
}

void rfbPixelFormat::print() const
{
    std::string message;
    if (bitsPerPixel.value() == 1) {
        message += fmt::format(
            "  Single bit per pixel.\n  {} significant bit in each byte is leftmost on the screen.",
            (bigEndian.value() ? "Most" : "Least"));
    }
    else {
        message += fmt::format("  {} bits per pixel.", bitsPerPixel.value());
        if (bitsPerPixel.value() != 8) {
            message += fmt::format("  {} significant byte first in each pixel.",
                                   (bigEndian.value() ? "Most" : "Least"));
        }
        if (trueColour.value()) {
            message += fmt::format("  TRUE colour: max red {} green {} blue {}"
                                   ", shift red {} green {} blue {}",
                                   redMax.value(),
                                   greenMax.value(),
                                   blueMax.value(),
                                   redShift.value(),
                                   greenShift.value(),
                                   blueShift.value());
        }
        else {
            message += fmt::format("  Colour map (not true colour).");
        }
    }
    spdlog::info(message);
}

} // namespace libvnc::proto