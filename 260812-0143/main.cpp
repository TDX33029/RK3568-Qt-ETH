#include "mainwindow.h"

#include <QApplication>
#include <QTimer>
#include <QStringList>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    /* --auto-live: 启动即收 ETH1/UDP(无需点按钮), 便于 offscreen/串口/无显示场景 */
    if (a.arguments().contains("--auto-live") || a.arguments().contains("-auto-live"))
        QTimer::singleShot(300, &w, &MainWindow::startLive);

    return QApplication::exec();
}
