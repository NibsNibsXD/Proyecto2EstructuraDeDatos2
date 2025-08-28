#ifndef TABLESPAGE_H
#define TABLESPAGE_H

#include <QWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTabWidget>
#include <QMap>
#include "datamodel.h"   // <--- NUEVO

class TablesPage : public QWidget {
    Q_OBJECT
public:
    explicit TablesPage(QWidget *parent = nullptr, bool withSidebar = true);
    QListWidget* tableListWidget() const { return tablesList; }

    // Exponer el esquema actual de una tabla (para RecordsPage)
    Schema schemaFor(const QString& table) const { return dbMock.value(table); }

signals:
    void tableSelected(const QString& tableName);                 // <--- NUEVO
    void schemaChanged(const QString& tableName, const Schema&);  // <--- NUEVO

private slots:
    void onAddField();
    void onRemoveField();
    void onNuevaTabla();
    void onEditarTabla();
    void onEliminarTabla();

    void onSelectTable();
    void onFieldSelectionChanged();
    void onPropertyChanged();
    void onNameItemEdited(QTableWidgetItem *it);

private:
    // Sidebar (lista de tablas)
    QListWidget  *tablesList = nullptr;

    // Barra superior
    QLineEdit    *tableNameEdit = nullptr;
    QLineEdit    *tableDescEdit = nullptr;
    QPushButton  *btnNueva = nullptr;
    QPushButton  *btnEditar = nullptr;
    QPushButton  *btnEliminar = nullptr;

    // Grid central de campos
    QTableWidget *fieldsTable = nullptr;
    QPushButton  *btnAddField = nullptr;
    QPushButton  *btnRemoveField = nullptr;

    // Propiedades abajo
    QTabWidget   *propTabs = nullptr;

    // General
    QLineEdit    *propFormato = nullptr;
    QLineEdit    *propMascara = nullptr;
    QLineEdit    *propTitulo = nullptr;
    QLineEdit    *propValorPred = nullptr;
    QLineEdit    *propReglaVal = nullptr;
    QLineEdit    *propTextoVal = nullptr;
    QCheckBox    *propRequerido = nullptr;
    QComboBox    *propIndexado = nullptr;

    // Datos en memoria: nombreTabla -> lista de campos
    QMap<QString, Schema> dbMock;     // <--- CAMBIO

    // Descripciones por tabla
    QMap<QString, QString> tableDesc_;

    // helpers UI
    void setupUi();
    void setupFakeData();
    void applyQss();
    void loadTableToUi(const QString &tableName);
    void loadFieldPropsToUi(const FieldDef &fd);
    void pullPropsFromUi(FieldDef &fd);
    void buildRowFromField(int row, const FieldDef &fd);
    void connectRowEditors(int row);
    QString currentTableName() const;

    void clearPropsUi();

    // helpers de validación/consulta
    bool tableExists(const QString& name) const;
    bool isValidTableName(const QString& name) const;

    bool withSidebar_ = true;
    QWidget *sidebarBox_ = nullptr;
};

#endif // TABLESPAGE_H
