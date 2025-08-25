#ifndef SHELLWINDOW_H
#define SHELLWINDOW_H

#pragma once
#include <QMainWindow>

class QListWidget;
class QStackedWidget;

class ShellWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ShellWindow(QWidget* parent=nullptr);
private:
    QListWidget*    sidebar_;
    QStackedWidget* stack_;
    void buildMenus();
};


#endif // SHELLWINDOW_H
