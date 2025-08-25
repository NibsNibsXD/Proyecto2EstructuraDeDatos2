#include "mainwindow.h"

#include <QApplication>

//Favor confirmar que este comentario aparece a la hora de hacer git clone. //#W#WAESKDPOASD SAKDSL:ADKASLDKSA:LDKASLDKAS:LKD:LASDa


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
