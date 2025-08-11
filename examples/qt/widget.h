#ifndef WIDGET_H
#define WIDGET_H

#include "libvnc-cpp/client.h"
#include <QWidget>

class Widget : public QWidget, public libvnc::client_delegate
{
    Q_OBJECT

public:
    Widget(QWidget* parent = nullptr);
    ~Widget();

protected:
    void on_connect(const libvnc::error& ec) override;
    void on_disconnect(const libvnc::error& ec) override;
    void on_frame_update(const libvnc::frame_buffer& buffer) override;
    std::string get_auth_password() const override;

    void paintEvent(QPaintEvent*) override;

private:
    boost::asio::io_context ioc_;
    libvnc::client client_;
    QImage image_;
};
#endif // WIDGET_H
