#include "widget.h"
#include <QPainter>
#include <QTimer>

Widget::Widget(QWidget* parent)
    : QWidget(parent)
    , client_(ioc_, this)
{
    auto timer = new QTimer(this);
    timer->setInterval(10);
    timer->start();
    connect(timer, &QTimer::timeout, this, [this]() { ioc_.poll(); });
    client_.set_host("127.0.0.1");
    //client_.set_host("100.64.0.15");
    client_.start();
}

Widget::~Widget()
{
}

void Widget::on_connect(const libvnc::error& ec)
{
    client_.send_set_monitor(1);
}

void Widget::on_disconnect(const libvnc::error& ec)
{
    client_.start();
}

void Widget::on_frame_update(const libvnc::frame_buffer& buffer)
{
    image_ = QImage(buffer.data(), buffer.width(), buffer.height(), QImage::Format_ARGB32);
    this->update();
}

void Widget::on_new_frame_size(int w, int h)
{
    this->resize(w, h);
}

void Widget::on_cursor_shape(int xhot,
                             int yhot,
                             const libvnc::frame_buffer& rc_source,
                             const uint8_t* rc_mask)
{
    auto image =
        QImage(rc_source.data(), rc_source.width(), rc_source.height(), QImage::Format_ARGB32);
    int w = rc_source.width();
    int h = rc_source.height();
    for (int x = 0; x < rc_source.width(); ++x) {
        for (int y = 0; y < rc_source.height(); ++y) {
            auto mask  = rc_mask[w * y + x];
            auto color = image.pixelColor(x, y);
            color.setAlphaF(mask);
            image.setPixelColor(x, y, color);
        }
    }
    QCursor cursor(QPixmap::fromImage(image_), xhot, yhot);
    // this->update();
}

void Widget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.drawImage(this->rect(), image_);
}
