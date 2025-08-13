#pragma once
#include "libvnc-cpp/proto.h"
#include <atomic>
#include <vector>

namespace libvnc {
class frame_buffer
{
public:
    void init(int w, int h, const proto::rfbPixelFormat& format);
    void set_size(int w, int h);
    void set_format(const proto::rfbPixelFormat& format);

    int width() const;
    int height() const;
    proto::rfbPixelFormat pixel_format() const;

    const uint8_t* data() const;
    uint8_t* data();
    std::size_t size() const;

    const uint8_t* data(int x, int y) const;
    uint8_t* data(int x, int y);

    uint8_t bytes_per_pixel() const;
    std::size_t bytes_per_line() const;

    bool check_rect(int x, int y, int w, int h) const;

    void got_bitmap(const uint8_t* buffer, int x, int y, int w, int h);
    void copy_rect(int src_x, int src_y, int w, int h, int dest_x, int dest_y);
    void fill_rect(int x, int y, int w, int h, const uint8_t* colour);

private:
    void malloc_frame_buffer();

private:
    std::atomic<int> width_  = 0;
    std::atomic<int> height_ = 0;
    proto::rfbPixelFormat format_;
    std::vector<uint8_t> data_;
};
} // namespace libvnc