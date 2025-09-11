#include <QApplication>
#include "recordspage.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    RecordsPage w;          // El .ui ya define 1140x574
    w.show();
    return app.exec();
}
