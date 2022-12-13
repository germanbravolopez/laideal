#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle(QStyleFactory::create("Windows"));
    MainWindow w;
    //w.setWindowState(Qt::WindowMaximized);
    w.show();
    return a.exec();
}
