#include "tablespage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>

TablesPage::TablesPage(QWidget* parent): QWidget(parent) {
    auto *root = new QVBoxLayout(this);

    auto *header = new QLabel("Estructura de Tabla (mock)");
    root->addWidget(header);

    grid_ = new QTableWidget(this);
    grid_->setColumnCount(5);
    grid_->setHorizontalHeaderLabels({"Nombre","Tipo","Tamaño","PK","Formato"});
    grid_->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(grid_, 1);

    auto *buttons = new QHBoxLayout();
    btnNew_    = new QPushButton("Nueva Tabla");
    btnEdit_   = new QPushButton("Editar");
    btnDelete_ = new QPushButton("Eliminar");
    buttons->addWidget(btnNew_);
    buttons->addWidget(btnEdit_);
    buttons->addWidget(btnDelete_);
    buttons->addStretch(1);
    root->addLayout(buttons);

    fillMock();
}

void TablesPage::fillMock(){
    grid_->setRowCount(3);
    grid_->setItem(0,0,new QTableWidgetItem("id"));
    grid_->setItem(0,1,new QTableWidgetItem("int"));
    grid_->setItem(0,2,new QTableWidgetItem("4"));
    grid_->setItem(0,3,new QTableWidgetItem("✔"));

    grid_->setItem(1,0,new QTableWidgetItem("nombre"));
    grid_->setItem(1,1,new QTableWidgetItem("string"));
    grid_->setItem(1,2,new QTableWidgetItem("60"));

    grid_->setItem(2,0,new QTableWidgetItem("email"));
    grid_->setItem(2,1,new QTableWidgetItem("string"));
    grid_->setItem(2,2,new QTableWidgetItem("80"));

    grid_->resizeColumnsToContents();
}
