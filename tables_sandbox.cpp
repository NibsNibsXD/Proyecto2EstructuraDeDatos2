#include <QApplication>
#include "tablespage.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    TablesPage page;
    page.resize(1140, 574);
    page.show();

    return app.exec();
}
