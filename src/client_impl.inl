#pragma once

namespace libvnc {
template<typename T>
void client_impl::copy_rect_from_rect(int src_x, int src_y, int w, int h, int dest_x, int dest_y)
{
    T* _buffer = ((T*)frame_buffer_.data()) + (src_y - dest_y) * width_ + src_x - dest_x;
    if (dest_y < src_y) {
        for (int j = dest_y * width_; j < (dest_y + h) * width_; j += width_) {
            if (dest_x < src_x) {
                for (int i = dest_x; i < dest_x + w; i++) {
                    ((T*)frame_buffer_.data())[j + i] = _buffer[j + i];
                }
            }
            else {
                for (int i = dest_x + w - 1; i >= dest_x; i--) {
                    ((T*)frame_buffer_.data())[j + i] = _buffer[j + i];
                }
            }
        }
    }
    else {
        for (int j = (dest_y + h - 1) * width_; j >= dest_y * width_; j -= width_) {
            if (dest_x < src_x) {
                for (int i = dest_x; i < dest_x + w; i++) {
                    ((T*)frame_buffer_.data())[j + i] = _buffer[j + i];
                }
            }
            else {
                for (int i = dest_x + w - 1; i >= dest_x; i--) {
                    ((T*)frame_buffer_.data())[j + i] = _buffer[j + i];
                }
            }
        }
    }
}


} // namespace libvnc