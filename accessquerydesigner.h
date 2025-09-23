#pragma once
#include <QWidget>
#include <QString>

class QComboBox;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QToolButton;
class QSpinBox;
class QLabel;

/**
 * Diseñador visual de consultas (estilo Access).
 * - Selección de tabla y columnas (lista de campos)
 * - Condiciones WHERE: N filas de OR; dentro de cada fila, AND por columna
 * - ORDER BY (columna + DESC)
 * - LIMIT
 * - Ejecutar: muestra resultados en un grid propio (abajo) y emite runSql(sql)
 * - Guardar / Guardar como / Renombrar / Eliminar vía DataModel
 */
class AccessQueryDesignerPage : public QWidget {
    Q_OBJECT
public:
    explicit AccessQueryDesignerPage(QWidget* parent=nullptr);

public slots:
    void setName(const QString& name);
    void setSqlText(const QString& sql);

signals:
    void runSql(const QString& sql);
    void savedQuery(const QString& name);

private slots:
    // columnas / grid
    void onTableChanged(const QString&);
    void onAddSelectedField();
    void onRemoveSelectedColumn();
    void onMoveLeft();
    void onMoveRight();
    void onClearGrid();
    void onAddOrRow();
    void onDelOrRow();

    // insertar operadores en la celda activa (Criteria/Or)
    void onInsertOpEq();
    void onInsertOpNe();
    void onInsertOpGt();
    void onInsertOpLt();
    void onInsertOpGe();
    void onInsertOpLe();
    void onInsertOpLike();
    void onInsertOpNotLike();
    void onInsertOpBetween();
    void onInsertOpIn();
    void onInsertOpNotIn();
    void onInsertIsNull();
    void onInsertNotNull();
    void onInsertTrue();
    void onInsertFalse();
    void onInsertQuotes();
    void onInsertParens();

    // acciones
    void onRun();
    void onSave();
    void onSaveAs();
    void onRename();
    void onDelete();

private:
    // helpers
    void rebuildFields();              // vuelve a llenar la lista de campos y ORDER BY
    QString buildSql() const;
    QString currentTable() const;      // implementado en .cpp
    void insertIntoActiveCriteriaCell(const QString& text);
    int     criteriaRowCount() const;  // filas OR actuales
    void    ensureGridHeaders();       // vuelve a rotular filas/columnas

    // ---- UI ----
    QComboBox*     cbTable_    {nullptr};
    QLineEdit*     edName_     {nullptr};

    QListWidget*   lwFields_   {nullptr};   // lista de campos de la tabla
    QTableWidget*  grid_       {nullptr};   // N filas (OR), N columnas = campos

    // ORDER / LIMIT
    QComboBox*     cbOrderBy_  {nullptr};
    QToolButton*   btnDesc_    {nullptr};
    QSpinBox*      spLimit_    {nullptr};

    // Preview + resultados
    QLabel*        sqlPreview_ {nullptr};   // texto SQL
    QTableWidget*  results_    {nullptr};   // resultados embebidos
    QLabel*        status_     {nullptr};   // estado (filas/SQL)

    QString        lastSqlText_;
};
