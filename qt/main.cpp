#include "widget.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("host", "Gateway host", "host");
    parser.process(a);
    Widget w(parser.positionalArguments().value(0, "127.0.0.1"));
    w.showFullScreen();
    return a.exec();
}
