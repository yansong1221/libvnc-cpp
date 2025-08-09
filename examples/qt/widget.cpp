#include "widget.h"
#include <QPainter>
Widget::Widget(boost::asio::io_context& ioc, QWidget* parent)
    : QWidget(parent)
    , client_(ioc, this, libvnc::proto::rfbPixelFormat(8, 3, 4), "127.0.0.1")
{
    client_.start();
}

Widget::~Widget()
{
}

void Widget::on_connect(const boost::system::error_code& ec)
{
}

void Widget::on_disconnect(const boost::system::error_code& ec)
{
}

void Widget::on_frame_update(const uint8_t* buffer)
{
    auto image = QImage(buffer, client_.get_width(), client_.get_height(), QImage::Format_ARGB32).copy();

    QMetaObject::invokeMethod(this, [this, image]() {
        image_ = image;
        this->update();
    });
    
    //if (image_.isNull())
    //    image_ = QImage(buffer, client_.get_width(), client_.get_height(), QImage::Format_ARGB32);
    //this->update();
}

void Widget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.drawImage(this->rect(), image_);
}
