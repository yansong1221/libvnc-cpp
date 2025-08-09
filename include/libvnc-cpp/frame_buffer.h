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
    const std::vector<uint8_t>& data() const;

    void got_bitmap(const uint8_t* buffer, int x, int y, int w, int h);
    void got_copy_rect(int src_x, int src_y, int w, int h, int dest_x, int dest_y);
    void got_fill_rect(int x, int y, int w, int h, uint32_t colour);

private:
    void malloc_frame_buffer();
    bool check_rect(int x, int y, int w, int h) const;

private:
    std::atomic<int> width_  = 0;
    std::atomic<int> height_ = 0;
    std::vector<uint8_t> buffer_;
    proto::rfbPixelFormat format_;
};
} // namespace libvnc