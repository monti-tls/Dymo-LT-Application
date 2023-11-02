#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "dymo_lt_ble_interface.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void M_requestBluetoothPermission();
    void M_dymoStateChanged(DymoLTBLEInterface::State state);
    void M_readDymoError();

private:
    Ui::MainWindow* m_ui;
    DymoLTBLEInterface* m_dymo;
    QPixmap m_label_pixmap;
};
#endif // MAINWINDOW_H
