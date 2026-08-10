#ifndef STATUSBAR_H
#define STATUSBAR_H

#include <QMainWindow>

namespace Ui {
class StatusBar;
}

class StatusBar : public QMainWindow
{
    Q_OBJECT

public:
    explicit StatusBar(QWidget *parent = nullptr);
    ~StatusBar();

private:
    Ui::StatusBar *ui;
};

#endif // STATUSBAR_H
