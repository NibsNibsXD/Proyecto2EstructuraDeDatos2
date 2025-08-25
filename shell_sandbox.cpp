#include <QApplication>
#include "shellwindow.h"

int main(int argc, char** argv){
    QApplication app(argc, argv);
    ShellWindow w; w.resize(1000,650); w.show();
    return app.exec();
}
