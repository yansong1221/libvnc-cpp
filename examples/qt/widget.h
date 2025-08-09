#ifndef WIDGET_H
#define WIDGET_H

#include "libvnc-cpp/client.h"
#include <QWidget>

class Widget : public QWidget, public libvnc::client_delegate
{
    Q_OBJECT

public:
    Widget(boost::asio::io_context& ioc, QWidget* parent = nullptr);
    ~Widget();

protected:
    void on_connect(const boost::system::error_code& ec) override;
    void on_disconnect(const boost::system::error_code& ec) override;
    void on_frame_update(const uint8_t* buffer) override;

    void paintEvent(QPaintEvent*) override;

private:
    libvnc::client client_;
    QImage image_;
};
#endif // WIDGET_H
