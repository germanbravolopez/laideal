#include "mainwindow.h"
#include "version.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle("fusion");
    MainWindow w;
    w.setWindowState(Qt::WindowMaximized);
    QIcon icon("C:/Users/rocio/OneDrive/Desktop/Tintoreria/change_icon/lavadora.ico");
    a.setWindowIcon(icon);
    w.setWindowIcon(icon);
    w.show();
    return a.exec();
}
