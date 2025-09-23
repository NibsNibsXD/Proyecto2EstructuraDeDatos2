#pragma once
#include <QWidget>
#include <QString>

class QComboBox; class QListWidget; class QTableWidget;
class QToolButton; class QSpinBox; class QLabel; class QLineEdit;

/**
 * Diseñador visual estilo Access:
 *  - Combo de Tabla + lista de Campos
 *  - Grid “Access-like” con columnas: Field | Table | Criteria | Or
 *  - Botonera de comparadores (=, <>, >, <, >=, <=, LIKE, BETWEEN, IN, IS NULL, …)
 *  - LIMIT y preview del SQL
 *  - Guardar / Renombrar / Eliminar en QueryStore (JSON)
 *  - Ejecutar (emite runSql(sql))
 *
 * Nota: Orden (ORDER BY) y Show/Hide se pueden añadir en una 2ª pasada.
 */
class AccessQueryDesignerPage : public QWidget {
    Q_OBJECT
public:
    explicit AccessQueryDesignerPage(QWidget* parent=nullptr);

    Q_SLOT void setName(const QString& name);
    Q_SLOT void setSqlText(const QString& sql); // solo muestra en preview (no parsea)

Q_SIGNALS:
    void savedQuery(const QString& name);
    void runSql(const QString& sql);

private Q_SLOTS:
    void onTableChanged(const QString& table);
    void onAddSelectedField();
    void onRemoveSelectedColumn();
    void onMoveLeft();
    void onMoveRight();
    void onClearGrid();

    // Insert helpers para comparadores (en la celda activa de Criteria/Or)
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

    void onRun();
    void onSave();
    void onSaveAs();
    void onRename();
    void onDelete();

private:
    void rebuildFields();
    QString buildSql() const;
    QString currentTable() const;
    void insertIntoActiveCriteriaCell(const QString& text);

    // UI (todo creado en código)
    QComboBox*     cbTable_   = nullptr;
    QListWidget*   lwFields_  = nullptr;
    QTableWidget*  grid_      = nullptr;   // 2 filas: Criteria y Or; n columnas = campos añadidos
    QSpinBox*      spLimit_   = nullptr;
    QLineEdit*     edName_    = nullptr;
    QLabel*        sqlPreview_= nullptr;
    QLabel*        status_    = nullptr;

    QString        lastSqlText_;
};
