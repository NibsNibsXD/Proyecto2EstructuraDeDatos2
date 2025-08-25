#include <QApplication>
#include "recordspage.h"

int main(int argc, char** argv){
    QApplication app(argc, argv);
    RecordsPage w; w.resize(900,550); w.show();
    return app.exec();
}
