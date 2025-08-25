#ifndef TABLESPAGE_H
#define TABLESPAGE_H

#pragma once
#include <QWidget>

class QTableWidget;
class QPushButton;

class TablesPage : public QWidget {
    Q_OBJECT
public:
    explicit TablesPage(QWidget* parent=nullptr);
private:
    QTableWidget* grid_;
    QPushButton *btnNew_, *btnEdit_, *btnDelete_;
    void fillMock(); // solo para ver diseño
};


#endif // TABLESPAGE_H
