#include "libvnc-cpp/frame_buffer.h"
#include <spdlog/spdlog.h>

namespace libvnc {

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
    return data_.data();
}

uint8_t* frame_buffer::data()
{
    return data_.data();
}

const uint8_t* frame_buffer::data(int x, int y) const
{
    return const_cast<frame_buffer*>(this)->data(x, y);
}

uint8_t* frame_buffer::data(int x, int y)
{
    size_t offset = std::size_t(y * width_ + x) * bytes_per_pixel();
    return data_.data() + offset;
}

std::size_t frame_buffer::size() const
{
    return data_.size();
}

uint8_t frame_buffer::bytes_per_pixel() const
{
    return format_.bitsPerPixel.value() / 8;
}

std::size_t frame_buffer::bytes_per_line() const
{
    return (std::size_t)width_ * bytes_per_pixel();
}

void frame_buffer::got_bitmap(const uint8_t* buffer, int x, int y, int w, int h)
{
    if (!check_rect(x, y, w, h)) {
        spdlog::warn("Rect out of bounds: {}x{} at ({}, {})", x, y, w, h);
        return;
    }
    std::size_t row_bytes = (std::size_t)w * bytes_per_pixel();

    for (int i = 0; i < h; ++i) {
        auto ptr = data(x, y + i);
        std::memcpy(ptr, buffer + row_bytes * i, row_bytes);
    }
}

void frame_buffer::copy_rect(int src_x, int src_y, int w, int h, int dest_x, int dest_y)
{
    if (!check_rect(src_x, src_y, w, h)) {
        spdlog::warn("Source rect out of bounds:{}x{} at ({}, {})", src_x, src_y, w, h);
        return;
    }

    if (!check_rect(dest_x, dest_y, w, h)) {
        spdlog::warn("Dest rect out of bounds: {}x{} at ({}, {})", dest_x, dest_y, w, h);
        return;
    }

    std::size_t row_bytes = (std::size_t)w * bytes_per_pixel();
    std::size_t all_bytes = row_bytes * h;

    std::vector<uint8_t> temp_buffer;
    temp_buffer.resize(all_bytes);

    for (int i = 0; i < h; ++i) {
        auto ptr = data(src_x, src_y + i);
        memcpy(temp_buffer.data() + row_bytes * i, ptr, row_bytes);
    }
    got_bitmap(temp_buffer.data(), dest_x, dest_y, w, h);
}

void frame_buffer::fill_rect(int x, int y, int w, int h, const uint8_t* colour)
{
    if (!check_rect(x, y, w, h)) {
        spdlog::warn("Rect out of bounds: {}x{} at ({}, {})", x, y, w, h);
        return;
    }
    auto bpp = bytes_per_pixel();

    for (int i = 0; i < h; ++i) {
        auto ptr = data(x, y + i);
        switch (bpp) {
            case 1: {
                std::fill(ptr, ptr + w, *colour);
            } break;
            case 2: {
                auto u16ptr = reinterpret_cast<uint16_t*>(ptr);
                std::fill(u16ptr, u16ptr + w, *reinterpret_cast<const uint16_t*>(colour));
            } break;
            case 4: {
                auto u32ptr = reinterpret_cast<uint32_t*>(ptr);
                std::fill(u32ptr, u32ptr + w, *reinterpret_cast<const uint32_t*>(colour));
            } break;
            default: spdlog::warn("Unsupported bitsPerPixel: {}", format_.bitsPerPixel.value());
        }
    }
}

void frame_buffer::malloc_frame_buffer()
{
    /* SECURITY: promote 'width' into uint64_t so that the multiplication does not overflow
        'width' and 'height' are 16-bit integers per RFB protocol design
        SIZE_MAX is the maximum value that can fit into size_t
       */
    auto allocSize = (uint64_t)width_ * height_ * format_.bitsPerPixel.value() / 8;
    data_.resize(allocSize, 0);
    data_.shrink_to_fit();
}

bool frame_buffer::check_overlap(int src_x, int src_y, int w, int h, int dest_x, int dest_y) const
{
    return !(src_x + w <= dest_x || src_x >= dest_x + w || src_y + h <= dest_y ||
             src_y >= dest_y + h);
}

bool frame_buffer::check_rect(int x, int y, int w, int h) const
{
    if (x < 0 || y < 0)
        return false;

    return x + w <= width_ && y + h <= height_;
}

} // namespace libvnc