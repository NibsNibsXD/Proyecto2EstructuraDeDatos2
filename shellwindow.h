#ifndef SHELLWINDOW_H
#define SHELLWINDOW_H

#pragma once
#include <QMainWindow>

class ShellWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ShellWindow(QWidget* parent=nullptr);
protected:
    void showEvent(QShowEvent* e) override;
};

#endif // SHELLWINDOW_H
