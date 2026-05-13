#include "mainwindow.h"
#include <QApplication>

// 程式進入點。Qt 會先建立 QApplication，再建立我們自己的 MainWindow。
// Demo 時可以說：main.cpp 只負責把遊戲視窗開起來，真正遊戲邏輯都在 MainWindow。
int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
