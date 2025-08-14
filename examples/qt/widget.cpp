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
    client_.start();
}

Widget::~Widget()
{
}

void Widget::on_connect(const libvnc::error& ec)
{
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

void Widget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.drawImage(this->rect(), image_);
}
