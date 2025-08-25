#include "shellwindow.h"
#include <QListWidget>
#include <QStackedWidget>
#include <QToolBar>
#include <QMenuBar>
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QApplication>

ShellWindow::ShellWindow(QWidget* parent): QMainWindow(parent) {
    setWindowTitle("MiniAccess - Shell (C++ puro)");
    auto *central = new QWidget(this);
    auto *root    = new QHBoxLayout(central);
    root->setContentsMargins(8,8,8,8);
    setCentralWidget(central);

    sidebar_ = new QListWidget(central);
    sidebar_->addItems({"Tablas","Registros","Consultas","Relaciones"});
    sidebar_->setFixedWidth(180);

    stack_ = new QStackedWidget(central);
    // páginas placeholder (cada equipo meterá su widget real luego)
    stack_->addWidget(new QLabel("Página: Tablas"));
    stack_->addWidget(new QLabel("Página: Registros"));
    stack_->addWidget(new QLabel("Página: Consultas"));
    stack_->addWidget(new QLabel("Página: Relaciones"));

    root->addWidget(sidebar_);
    root->addWidget(stack_, 1);

    connect(sidebar_, &QListWidget::currentRowChanged,
            stack_,   &QStackedWidget::setCurrentIndex);
    sidebar_->setCurrentRow(0);

    buildMenus();
}

void ShellWindow::buildMenus(){
    auto *file = menuBar()->addMenu("&Archivo");
    file->addAction("Salir", this, []{ qApp->quit(); });
    addToolBar(Qt::TopToolBarArea, new QToolBar("Main", this));
}
