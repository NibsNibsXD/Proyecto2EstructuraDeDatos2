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
#include "datamodel.h"   // <- Define FieldDef y using Schema = QList<FieldDef>

class TablesPage : public QWidget {
    Q_OBJECT
public:
    explicit TablesPage(QWidget *parent = nullptr, bool withSidebar = true);

    // Accesos para integración
    QListWidget* tableListWidget() const { return tablesList; }
    Schema       schemaFor(const QString& tableName) const { return dbMock.value(tableName); }
    QStringList  tableNames() const { return dbMock.keys(); }

signals:
    // Notifica a Shell/RecordsPage
    void tableSelected(const QString& name);
    void schemaChanged(const QString& name, const Schema& schema);
    void tablesListChanged(const QStringList& names);

private slots:
    // acciones de tabla/campos
    void onAddField();
    void onRemoveField();
    void onNuevaTabla();
    void onEditarTabla();
    void onEliminarTabla();

    // sincronización UI <-> modelo
    void onSelectTable();
    void onFieldSelectionChanged();
    void onPropertyChanged();
    void onNameItemEdited(QTableWidgetItem *it);

private:
    // Sidebar (lista de tablas)
    QListWidget  *tablesList = nullptr;

    // Barra superior
    QLineEdit    *tableNameEdit = nullptr;
    QLineEdit    *tableDescEdit = nullptr;     // descripción de tabla
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

    // Datos en memoria: nombreTabla -> lista de campos (Schema)
    QMap<QString, Schema> dbMock;

    // Descripciones por tabla
    QMap<QString, QString> tableDesc_;   // nombreTabla -> descripción

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

    // limpiar panel de propiedades
    void clearPropsUi();

    // validación/consulta
    bool tableExists(const QString& name) const;
    bool isValidTableName(const QString& name) const;

    bool withSidebar_ = true;
    QWidget *sidebarBox_ = nullptr;
};

#endif // TABLESPAGE_H
