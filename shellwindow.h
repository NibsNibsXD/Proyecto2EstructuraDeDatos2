#ifndef SHELLWINDOW_H
#define SHELLWINDOW_H

#pragma once

#include <QMainWindow>
#include <QToolButton>
#include <QStackedWidget>
#include <QDockWidget>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QAction>

class QListWidgetItem;
class QShowEvent;
class QCloseEvent;

// Fwds de páginas (evita incluir headers pesados aquí)
class QueryDesigner;
class QueryPage;

class ShellWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ShellWindow(QWidget* parent=nullptr);

protected:
    void showEvent(QShowEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private:
    /* =================== Top-level UI =================== */
    // pestañas superiores (Home, Create, DB Tools)
    QToolButton*   homeBtn    = nullptr;
    QToolButton*   createBtn  = nullptr;
    QToolButton*   dbToolsBtn = nullptr;

    // barra de 100px (donde se muestran los grupos/botones del menú activo)
    QWidget*        iconBar      = nullptr;
    QStackedWidget* ribbonStack  = nullptr;  // páginas: Home / Create / DBTools

    // área central para alternar páginas (ej. Diseñador / Resultados)
    QStackedWidget* centerStack  = nullptr;  // index 0: QueryDesigner, 1: QueryPage (resultados)

    /* =================== Sidebar (Consultas) =================== */
    QDockWidget*    leftDock         = nullptr;
    QWidget*        leftDockBody     = nullptr;
    QListWidget*    queriesList      = nullptr; // lista de consultas guardadas
    QToolButton*    newQueryBtn      = nullptr; // atajo arriba de la lista
    QToolButton*    renameQueryBtn   = nullptr;
    QToolButton*    deleteQueryBtn   = nullptr;

    // menú contextual sobre la lista
    QMenu*          queriesCtxMenu   = nullptr;
    QAction*        actNewQuery      = nullptr;
    QAction*        actRenameQuery   = nullptr;
    QAction*        actDeleteQuery   = nullptr;
    QAction*        actOpenDesigner  = nullptr;
    QAction*        actRunQuery      = nullptr;

    /* =================== Páginas =================== */
    QueryDesigner*  designerPage     = nullptr; // editor visual / SQL
    QueryPage*      resultsPage      = nullptr; // resultados (tabla)

private:
    /* =================== Helpers UI =================== */
    QWidget* buildHomeRibbon();      // botones de Home
    QWidget* buildCreateRibbon();    // placeholder por ahora
    QWidget* buildDBToolsRibbon();   // placeholder por ahora

    void     buildLeftSidebar();     // crea el dock con la lista de consultas
    void     buildCenterPages();     // crea designerPage y resultsPage
    void     connectModelSignals();  // conecta señales del DataModel
    void     wireActions();          // conecta botones/acciones con slots

private slots:
    /* =================== Slots de sidebar/acciones =================== */
    void refreshSavedQueries();                    // repuebla queriesList
    void onQueryActivated(QListWidgetItem* it);    // doble click / Enter
    void onShowContextMenu(const QPoint& pos);     // menú contextual

    void onNewQuery();                             // crear consulta
    void onRenameSelectedQuery();                  // renombrar consulta
    void onDeleteSelectedQuery();                  // eliminar consulta
    void onOpenInDesigner();                       // abrir en editor
    void onRunSelectedQuery();                     // ejecutar y mostrar resultados

    // desde el diseñador: guardar o “guardar como” debe refrescar la lista
    void onDesignerSaved(const QString& name);     // actualizar selección / refrescar
};

#endif // SHELLWINDOW_H
