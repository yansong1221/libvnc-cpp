#include "libvnc-cpp/frame_buffer.h"
#include <spdlog/spdlog.h>

namespace libvnc {

namespace detail {

template<typename T>
void copy_rect_from_rect(uint8_t* frame_buffer,
                         int frame_w,
                         int frame_h,
                         int src_x,
                         int src_y,
                         int w,
                         int h,
                         int dest_x,
                         int dest_y)
{
    T* _buffer = ((T*)frame_buffer) + (src_y - dest_y) * frame_w + src_x - dest_x;
    if (dest_y < src_y) {
        for (int j = dest_y * frame_w; j < (dest_y + h) * frame_w; j += frame_w) {
            if (dest_x < src_x) {
                for (int i = dest_x; i < dest_x + w; i++) {
                    ((T*)frame_buffer)[j + i] = _buffer[j + i];
                }
            }
            else {
                for (int i = dest_x + w - 1; i >= dest_x; i--) {
                    ((T*)frame_buffer)[j + i] = _buffer[j + i];
                }
            }
        }
    }
    else {
        for (int j = (dest_y + h - 1) * frame_w; j >= dest_y * frame_w; j -= frame_w) {
            if (dest_x < src_x) {
                for (int i = dest_x; i < dest_x + w; i++) {
                    ((T*)frame_buffer)[j + i] = _buffer[j + i];
                }
            }
            else {
                for (int i = dest_x + w - 1; i >= dest_x; i--) {
                    ((T*)frame_buffer)[j + i] = _buffer[j + i];
                }
            }
        }
    }
}
} // namespace detail

void frame_buffer::init(int w, int h, const proto::rfbPixelFormat& format)
{
    width_  = w;
    height_ = h;
    format_ = format;
    malloc_frame_buffer();
}

void frame_buffer::set_size(int w, int h)
{
    if (width_ == w && height_ == h)
        return;

    width_  = w;
    height_ = h;

    malloc_frame_buffer();
}

void frame_buffer::set_format(const proto::rfbPixelFormat& format)
{
    bool changed = format_.bitsPerPixel.value() != format.bitsPerPixel.value();

    format_ = format;
    if (changed)
        malloc_frame_buffer();
}

int frame_buffer::width() const
{
    return width_;
}

int frame_buffer::height() const
{
    return height_;
}

proto::rfbPixelFormat frame_buffer::pixel_format() const
{
    return format_;
}

const uint8_t* frame_buffer::data() const
{
    return buffer_.data();
}

uint8_t* frame_buffer::data()
{
    return buffer_.data();
}

std::size_t frame_buffer::size() const
{
    return buffer_.size();
}

uint8_t frame_buffer::bytes_per_pixel() const
{
    return format_.bitsPerPixel.value() / 8;
}

void frame_buffer::got_bitmap(const uint8_t* buffer, int x, int y, int w, int h)
{
    if (!check_rect(x, y, w, h)) {
        spdlog::warn("Rect out of bounds: {}x{} at ({}, {})", x, y, w, h);
        return;
    }

    auto BPP = format_.bitsPerPixel.value();
    if (BPP != 8 && BPP != 16 && BPP != 32) {
        spdlog::warn("Unsupported bitsPerPixel: {}", BPP);
        return;
    }

    int rs = w * BPP / 8, rs2 = width_ * BPP / 8;
    for (int j = ((x * (BPP / 8)) + (y * rs2)); j < (y + h) * rs2; j += rs2) {
        memcpy(data() + j, buffer, rs);
        buffer += rs;
    }
}

void frame_buffer::got_copy_rect(int src_x, int src_y, int w, int h, int dest_x, int dest_y)
{
    if (!check_rect(src_x, src_y, w, h)) {
        spdlog::warn("Source rect out of bounds:{}x{} at ({}, {})", src_x, src_y, w, h);
        return;
    }

    if (!check_rect(dest_x, dest_y, w, h)) {
        spdlog::warn("Dest rect out of bounds: {}x{} at ({}, {})", dest_x, dest_y, w, h);
        return;
    }

    switch (format_.bitsPerPixel.value()) {
        case 8:
            detail::copy_rect_from_rect<uint8_t>(
                data(), width_, height_, src_x, src_y, w, h, dest_x, dest_y);
            break;
        case 16:
            detail::copy_rect_from_rect<uint16_t>(
                data(), width_, height_, src_x, src_y, w, h, dest_x, dest_y);
            break;
        case 32:
            detail::copy_rect_from_rect<uint32_t>(
                data(), width_, height_, src_x, src_y, w, h, dest_x, dest_y);
            break;
        default: spdlog::warn("Unsupported bitsPerPixel: {}", format_.bitsPerPixel.value());
    }
}

void frame_buffer::got_fill_rect(int x, int y, int w, int h, uint32_t colour)
{
    if (!check_rect(x, y, w, h)) {
        spdlog::warn("Rect out of bounds: {}x{} at ({}, {})", x, y, w, h);
        return;
    }
    auto fill_rect = [this]<typename T>(int x, int y, int w, int h, T colour) {
        auto ptr = reinterpret_cast<T*>(data());
        for (int j = y * width_; j < (y + h) * width_; j += width_)
            for (int i = x; i < x + w; i++)
                ptr[j + i] = colour;
    };

    switch (format_.bitsPerPixel.value()) {
        case 8: fill_rect(x, y, w, h, (uint8_t)colour); break;
        case 16: fill_rect(x, y, w, h, (uint16_t)colour); break;
        case 32: fill_rect(x, y, w, h, (uint32_t)colour); break;
        default: spdlog::warn("Unsupported bitsPerPixel: {}", format_.bitsPerPixel.value());
    }
}

void frame_buffer::malloc_frame_buffer()
{
    /* SECURITY: promote 'width' into uint64_t so that the multiplication does not overflow
        'width' and 'height' are 16-bit integers per RFB protocol design
        SIZE_MAX is the maximum value that can fit into size_t
       */
    auto allocSize = (uint64_t)width_ * height_ * format_.bitsPerPixel.value() / 8;
    buffer_.resize(allocSize, 0);
}

bool frame_buffer::check_rect(int x, int y, int w, int h) const
{
    return x + w <= width_ && y + h <= height_;
}

} // namespace libvnc