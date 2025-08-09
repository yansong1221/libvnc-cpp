#include "widget.h"

#include "libvnc-cpp/client.h"
#include <QApplication>
#include <QTimer>

int main(int argc, char* argv[])
{
    boost::asio::io_context ioc;

    auto tp = std::thread([&]() {
        auto work = boost::asio::make_work_guard(ioc);
        ioc.run();
    });

    QApplication a(argc, argv);

    // QTimer timer;
    // timer.setInterval(10);
    // QObject::connect(&timer, &QTimer::timeout, &a, [&]() { ioc.poll(); });
    // timer.start();

    Widget w(ioc);
    w.show();
    a.exec();
    tp.join();
}
