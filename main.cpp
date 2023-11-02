#include "main_window.h"

#include <QApplication>
#include <QFontDatabase>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    qDebug() << QFontDatabase::families();
    
    MainWindow w;
    w.show();
    return a.exec();
}
