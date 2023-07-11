#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle("fusion");
    MainWindow w;
    w.setWindowState(Qt::WindowMaximized);
    QIcon icon("C:/Users/Usuario/OneDrive/Desktop/Tintoreria/NO_TOCAR_ticket_imprimir/lavadora.ico");
    a.setWindowIcon(icon);
    w.setWindowIcon(icon);
    w.show();
    return a.exec();
}
