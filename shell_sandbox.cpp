#include <QApplication>
#include "shellwindow.h"

int main(int argc, char** argv){
    QApplication app(argc, argv);
    ShellWindow w; w.resize(1400,800); w.show();
    return app.exec();
}
