#include "recordspage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>

RecordsPage::RecordsPage(QWidget* parent): QWidget(parent){
    auto *root = new QVBoxLayout(this);

    auto *top = new QHBoxLayout();
    tableSelector_ = new QComboBox(this);
    tableSelector_->addItems({"Clientes","Pedidos"});
    top->addWidget(new QLabel("Tabla:"));
    top->addWidget(tableSelector_);
    top->addStretch(1);
    root->addLayout(top);

    grid_ = new QTableWidget(this);
    root->addWidget(grid_, 1);

    connect(tableSelector_, &QComboBox::currentTextChanged,
            this, &RecordsPage::loadMock);

    loadMock("Clientes");
}

void RecordsPage::loadMock(const QString& t){
    if(t=="Clientes"){
        grid_->clear();
        grid_->setColumnCount(3);
        grid_->setHorizontalHeaderLabels({"id","nombre","email"});
        grid_->setRowCount(2);
        grid_->setItem(0,0,new QTableWidgetItem("1"));
        grid_->setItem(0,1,new QTableWidgetItem("Ana"));
        grid_->setItem(0,2,new QTableWidgetItem("ana@mail.com"));
        grid_->setItem(1,0,new QTableWidgetItem("2"));
        grid_->setItem(1,1,new QTableWidgetItem("Luis"));
        grid_->setItem(1,2,new QTableWidgetItem("luis@mail.com"));
    } else {
        grid_->clear();
        grid_->setColumnCount(3);
        grid_->setHorizontalHeaderLabels({"id","cliente_id","total"});
        grid_->setRowCount(2);
        grid_->setItem(0,0,new QTableWidgetItem("101"));
        grid_->setItem(0,1,new QTableWidgetItem("1"));
        grid_->setItem(0,2,new QTableWidgetItem("59.9"));
        grid_->setItem(1,0,new QTableWidgetItem("102"));
        grid_->setItem(1,1,new QTableWidgetItem("2"));
        grid_->setItem(1,2,new QTableWidgetItem("120.0"));
    }
    grid_->resizeColumnsToContents();
}
