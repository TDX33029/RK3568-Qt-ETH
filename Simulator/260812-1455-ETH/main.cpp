#include "mainwindow.h"

#include <QApplication>
#include <QTimer>
#include <QStringList>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    if (a.arguments().contains("--auto-live") || a.arguments().contains("-auto-live"))
        QTimer::singleShot(300, &w, &MainWindow::startLive);

    return QApplication::exec();
}
