#include "main_window.h"
#include "ui_main_window.h"

#include <QPixmap>
#include <QPainter>
#include <QBluetoothPermission>
#include <QFontDatabase>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    m_ui(new Ui::MainWindow),
    m_dymo(new DymoLTBLEInterface(this))
{
    m_ui->setupUi(this);

    m_ui->font_combo->addItems(QFontDatabase::families());
    m_ui->font_combo->setCurrentText("Helvetica");

    connect(m_dymo, &DymoLTBLEInterface::stateChanged, this, &MainWindow::M_dymoStateChanged);
    connect(m_dymo, &DymoLTBLEInterface::errorOccured, this, &MainWindow::M_readDymoError);

    auto draw = [this](QString const& line1, QString const& line2 = QString()) -> QPixmap
    {
        QFont font(m_ui->font_combo->currentText());
        font.setPixelSize(line2.isEmpty() ? 36 : 18);
        font.setWeight(line2.isEmpty() ? QFont::DemiBold : QFont::DemiBold);
        font.setStretch(line2.isEmpty() ? QFont::SemiCondensed : QFont::SemiExpanded);

        int width = qMax(QFontMetrics(font).tightBoundingRect(line1).width(),
                         QFontMetrics(font).tightBoundingRect(line2).width());

        QBitmap pix(width ? width : 32, 32);
        QPainter* paint = new QPainter(&pix);
        paint->setRenderHint(QPainter::TextAntialiasing);
        paint->setPen(Qt::white);
        paint->setBrush(Qt::white);
        paint->drawRect(QRect(QPoint(0, 0), pix.size()));
        paint->setFont(font);
        paint->setPen(Qt::black);

        if (line2.isEmpty())
            paint->drawText(QRect(QPoint(0, 4), pix.size()), Qt::AlignHCenter| Qt::AlignVCenter, line1);
        else
        {
            paint->drawText(QRect(0, 2, pix.width(), pix.height()/2), Qt::AlignHCenter| Qt::AlignVCenter, line1);
            paint->drawText(QRect(0, 2+pix.height()/2, pix.width(), pix.height()/2), Qt::AlignHCenter| Qt::AlignVCenter, line2);
        }
        delete paint;

        return pix;
    };

    auto update_label = [this, draw]()
    {
        m_label_pixmap = draw(m_ui->line1_edit->text().toUpper(),
                              m_ui->line2_edit->text().toUpper());

        m_ui->label_preview->resize(m_label_pixmap.size());
        m_ui->label_preview->setPixmap(m_label_pixmap);
    };

    connect(m_ui->line1_edit, &QLineEdit::textChanged, this, update_label);
    connect(m_ui->line2_edit, &QLineEdit::textChanged, this, update_label);
    connect(m_ui->font_combo, &QComboBox::currentIndexChanged, this, update_label);

    connect(m_ui->clear_button, &QAbstractButton::pressed, m_ui->line1_edit, &QLineEdit::clear);
    connect(m_ui->clear_button, &QAbstractButton::pressed, m_ui->line2_edit, &QLineEdit::clear);

    connect(m_ui->go_button, &QPushButton::pressed, this, [this]()
    {
        m_ui->error_label->clear();
        QImage img = m_label_pixmap.toImage().mirrored(true, false);

        QBitArray pixels(img.width() * img.height(), false);
        for (int y = 0; y < img.width(); ++y)
            for (int x = 0; x < img.height(); ++x)
                pixels[x+y*img.height()] = (img.pixel(y, x) & 0xFFFFFF) == 0;
        m_dymo->print(pixels);
    });

    update_label();

    M_requestBluetoothPermission();
}

MainWindow::~MainWindow()
{
}

void MainWindow::M_requestBluetoothPermission()
{
    QBluetoothPermission p;
    switch (qApp->checkPermission(p))
    {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(p, this, &MainWindow::M_requestBluetoothPermission);

    case Qt::PermissionStatus::Denied:
        qApp->quit();
        break;

    case Qt::PermissionStatus::Granted:
        break;
    }
}

void MainWindow::M_dymoStateChanged(DymoLTBLEInterface::State state)
{
    m_ui->go_button->setEnabled(state == DymoLTBLEInterface::State::Idle);
    qDebug() << state;
}

void MainWindow::M_readDymoError()
{
    QString error = m_dymo->readError();
    m_ui->error_label->setText(error);

    qDebug() << error;
}
