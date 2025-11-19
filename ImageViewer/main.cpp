// main.cpp
#include <QApplication>
#include "mainwindow.h"

/*
 * アプリケーションエントリポイント。
 *   - QApplication を生成（Qt のイベントループ）
 *   - MainWindow を生成＆表示
 *   - app.exec() でイベントループ開始
 *
 * Qt に不慣れな場合も、ここは「WinMain のようなもの」と考えてよい。
 */
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    MainWindow w;
    w.show();

    return app.exec();
}
