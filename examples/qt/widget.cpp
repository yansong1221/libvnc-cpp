#include "widget.h"
#include <QPainter>
#include <QTimer>

Widget::Widget(QWidget* parent)
    : QWidget(parent)
    , client_(ioc_)
{
    auto timer = new QTimer(this);
    timer->setInterval(10);
    timer->start();
    connect(timer, &QTimer::timeout, this, [this]() { ioc_.poll(); });
    client_.set_host("127.0.0.1");
    client_.set_delegate(this);
    client_.start();
}

Widget::~Widget()
{
    client_.set_delegate(nullptr);
}

void Widget::on_connect(const libvnc::error& ec)
{
    if (ec.is_system_error()) {
        ec.value();
    }
    client_.send_client_cut_text_utf8("111111111111");
    //client_.send_frame_encodings({"hextile"});
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

std::string Widget::get_auth_password() const
{
    return "123456";
}

void Widget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.drawImage(this->rect(), image_);
}
