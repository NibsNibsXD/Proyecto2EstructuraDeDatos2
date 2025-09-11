#ifndef RECORDSPAGE_H
#define RECORDSPAGE_H

#pragma once
#include <QWidget>
//test
class QComboBox;
class QTableWidget;

class RecordsPage : public QWidget {
    Q_OBJECT
public:
    explicit RecordsPage(QWidget* parent=nullptr);
private:
    QComboBox* tableSelector_;
    QTableWidget* grid_;
    void loadMock(const QString& table);
};


#endif // RECORDSPAGE_H
