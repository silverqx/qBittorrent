#ifndef QRIGHTCLICKBUTTON_H
#define QRIGHTCLICKBUTTON_H

#include <QMouseEvent>
#include <QPushButton>

class QRightClickButton : public QPushButton
{
    Q_OBJECT

public:
    explicit QRightClickButton(QWidget *parent = 0);

signals:
    void rightClicked();

    // QWidget interface
protected:
    void mousePressEvent(QMouseEvent* event) override;
};

#endif // QRIGHTCLICKBUTTON_H
