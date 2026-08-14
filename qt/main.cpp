#include <QApplication>
#include <QTimer>
#include <QDebug>
#include "mainwindow.h"

// Qt 端入口:用法 ./qt_gateway [网关IP] [-screenshot out.png]
//   - 网关 IP 缺省 127.0.0.1(板上与网关同机)
//   - 显示平台由 -platform 指定(linuxfb / vnc),由 QApplication 自动消费
//   - -screenshot:渲染 3 秒后把界面存 PNG 并退出(无头验证用,offscreen 平台)
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QString ip = "127.0.0.1";
    QString shotPath;

    // 过滤 Qt 已消费的 -platform 等参数后的剩余参数
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i)
    {
        if (args[i] == "-screenshot" && i + 1 < args.size())
        {
            shotPath = args[i + 1];
            ++i;
        }
        else if (!args[i].startsWith('-'))
        {
            ip = args[i];
        }
    }

    MainWindow w(ip);
    w.show();

    if (!shotPath.isEmpty())
    {
        // 无头验证:等 3 秒(让首轮轮询/渲染完成)后抓图保存退出
        QTimer::singleShot(3000, [&]() {
            if (w.grab().save(shotPath))
                qInfo() << "screenshot saved:" << shotPath;
            else
                qWarning() << "screenshot FAILED:" << shotPath;
            app.quit();
        });
    }

    return app.exec();
}
