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
 * - Selección de tabla y columnas
 * - Condiciones WHERE (2 filas: Criteria / Or)
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

    // insertar operadores en la celda activa (Criteria/Or)
    void onInsertOpEq();
    void onInsertOpNe();
    void onInsertOpGt();
    void onInsertOpLt();
    void onInsertOpGe();
    void onInsertOpLe();
    void onInsertOpLike();
    void onInsertOpBetween();
    void onInsertOpIn();
    void onInsertIsNull();
    void onInsertNotNull();
    void onInsertTrue();
    void onInsertFalse();

    // acciones
    void onRun();
    void onSave();
    void onSaveAs();
    void onRename();
    void onDelete();

private:
    // helpers
    void rebuildFields();
    QString buildSql() const;
    QString currentTable() const;             // <— solo declaración (sin cuerpo aquí)
    void insertIntoActiveCriteriaCell(const QString& text);

    // ---- UI ----
    QComboBox*     cbTable_    {nullptr};
    QLineEdit*     edName_     {nullptr};

    QListWidget*   lwFields_   {nullptr};   // lista de campos de la tabla
    QTableWidget*  grid_       {nullptr};   // 2 filas (Criteria/Or), N columnas = campos

    QSpinBox*      spLimit_    {nullptr};

    QLabel*        sqlPreview_ {nullptr};   // texto SQL
    QTableWidget*  results_    {nullptr};   // resultados embebidos
    QLabel*        status_     {nullptr};   // estado (filas/SQL)

    QString        lastSqlText_;
};
