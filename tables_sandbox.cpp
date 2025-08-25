#include <QApplication>
#include "tablespage.h"

int main(int argc, char** argv){
    QApplication app(argc, argv);
    TablesPage w; w.resize(800,500); w.show();
    return app.exec();
}
