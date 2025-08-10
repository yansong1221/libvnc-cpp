#include "widget.h"
#include <QPainter>
Widget::Widget(boost::asio::io_context& ioc, QWidget* parent)
    : QWidget(parent)
    , client_(ioc, this, "127.0.0.1")
{
    client_.start();
}

Widget::~Widget()
{
}

void Widget::on_connect(const libvnc::error& ec)
{
    if (ec.is_system_error())
    {
        ec.value();
    }
    
}

void Widget::on_disconnect(const libvnc::error& ec)
{
}

void Widget::on_frame_update(const libvnc::frame_buffer& buffer)
{
    auto image = QImage(buffer.data().data(), buffer.width(), buffer.height(), QImage::Format_ARGB32).copy();

    QMetaObject::invokeMethod(this, [this, image]() {
        image_ = image;
        this->update();
    });
    
    //if (image_.isNull())
    //    image_ = QImage(buffer, client_.get_width(), client_.get_height(), QImage::Format_ARGB32);
    //this->update();
}

std::string Widget::get_auth_password() const
{
    return "123456";
}

void Widget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.drawImage(this->rect(), image_);
}
