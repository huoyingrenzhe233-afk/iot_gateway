#include "statusbar.h"
#include "ui_statusbar.h"

StatusBar::StatusBar(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::StatusBar)
{
    ui->setupUi(this);
}

StatusBar::~StatusBar()
{
    delete ui;
}
