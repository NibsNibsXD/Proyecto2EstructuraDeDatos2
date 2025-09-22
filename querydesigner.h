#pragma once
#include <QWidget>
#include "datamodel.h"


class QComboBox; class QListWidget; class QTableWidget;
class QToolButton; class QSpinBox; class QLabel; class QLineEdit;

/**
 * Constructor visual de SELECT (Access-like):
 *  - Tabla
 *  - Columnas (checklist)
 *  - Condiciones (Field/Op/Value)
 *  - Order By, DESC?, Limit
 *  - Ejecutar, Guardar, Renombrar, Eliminar
 *  - Grid de resultados
 */
class QueryDesignerPage : public QWidget {
    Q_OBJECT
public:
    explicit QueryDesignerPage(QWidget* parent=nullptr);

signals:
    void savedQuery(const QString& name); // para refrescar la lista de la izquierda

private slots:
    void onTableChanged(const QString& table);
    void onAddCond();
    void onDelCond();
    void onRun();
    void onSave();
    void onRename();
    void onDelete();

private:
    void rebuildColumns();
    QString buildSql() const;
    void execSelectSql(const QString& sql);

    // helpers de comparación (copiados de QueryPage)
    static int schemaFieldIndex(const Schema& s, const QString& name);
    static int cmpVar(const QVariant& a, const QVariant& b);
    static bool equalVar(const QVariant& a, const QVariant& b);

private:
    QComboBox*   cbTable_;
    QListWidget* lwColumns_;
    QTableWidget* twWhere_;
    QComboBox*   cbOrderBy_;
    QToolButton* btnDesc_;
    QSpinBox*    spLimit_;
    QLineEdit*   edName_;
    QTableWidget* grid_;
    QLabel*      status_;
};
