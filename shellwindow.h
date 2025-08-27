#ifndef SHELLWINDOW_H
#define SHELLWINDOW_H

#include <QToolButton>
#include <QStackedWidget>
#pragma once
#include <QMainWindow>

class ShellWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ShellWindow(QWidget* parent=nullptr);
protected:
    void showEvent(QShowEvent* e) override;
private:
    // pestañas superiores (Home, Create, DB Tools)
    QToolButton* homeBtn     = nullptr;
    QToolButton* createBtn   = nullptr;
    QToolButton* dbToolsBtn  = nullptr;

    // barra de 100px (donde se muestran los grupos/botones del menú activo)
    QWidget*       iconBar     = nullptr;
    QStackedWidget* ribbonStack = nullptr; // páginas: Home / Create / DBTools

    // helpers UI
    QWidget* buildHomeRibbon();      // ← botones de Home
    QWidget* buildCreateRibbon();    // (placeholder por ahora)
    QWidget* buildDBToolsRibbon();   // (placeholder por ahora)
};

#endif // SHELLWINDOW_H
