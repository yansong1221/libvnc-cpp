#include "widget.h"

#include "libvnc-cpp/client.h"
#include <QApplication>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    // QTimer timer;
    // timer.setInterval(10);
    // QObject::connect(&timer, &QTimer::timeout, &a, [&]() { ioc.poll(); });
    // timer.start();

    Widget w;
    w.show();
    a.exec();
}
