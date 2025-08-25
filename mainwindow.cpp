#include "mainwindow.h"
#include <QLabel>

MainWindow::MainWindow(QWidget* parent): QMainWindow(parent){
    setWindowTitle("MiniAccess - App");
    setCentralWidget(new QLabel("App integrada (aquí luego embebes páginas)"));
}
