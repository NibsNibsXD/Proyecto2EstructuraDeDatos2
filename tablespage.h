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

#include "datamodel.h"   // FieldDef / Schema / DataModel

class TablesPage : public QWidget {
    Q_OBJECT
public:
    explicit TablesPage(QWidget *parent = nullptr, bool withSidebar = true);
    QListWidget* tableListWidget() const { return tablesList; }

    // Compatibilidad con ShellWindow: toma el esquema desde el DataModel
    Schema schemaFor(const QString& table) const { return DataModel::instance().schema(table); }

signals:
    void tableSelected(const QString& tableName);
    void schemaChanged(const QString& tableName, const Schema&);

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

    // Estado actual (tabla seleccionada y su esquema)
    QString m_currentTable;
    Schema  m_currentSchema;

    // Descripciones por tabla (solo UI de maqueta)
    QMap<QString, QString> tableDesc_;

    // helpers UI
    void setupUi();
    void setupFakeData();             // ahora siembra el DataModel
    void applyQss();
    void loadTableToUi(const QString &tableName);
    void loadFieldPropsToUi(const FieldDef &fd);
    void pullPropsFromUi(FieldDef &fd);
    void buildRowFromField(int row, const FieldDef &fd);
    void connectRowEditors(int row);
    QString currentTableName() const;

    void clearPropsUi();

    // NUEVOS helpers usados por el .cpp
    void updateTablesList(const QString& preferSelect = QString());
    bool applySchemaAndRefresh(const Schema& s, int preserveRow = -1);

    // validación
    bool isValidTableName(const QString& name) const;

    bool withSidebar_ = true;
    QWidget *sidebarBox_ = nullptr;
};

#endif // TABLESPAGE_H
