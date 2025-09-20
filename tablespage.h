#ifndef TABLESPAGE_H
#define TABLESPAGE_H

#include <QWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTabWidget>
#include <QMap>
#include <QSignalBlocker>
#include <QTabBar>

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
protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

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
    QLineEdit    *tableDescEdit = nullptr;     // descripción de tabla (opcional)
    QPushButton  *btnNueva = nullptr;
    QPushButton  *btnEditar = nullptr;
    QPushButton  *btnEliminar = nullptr;

    // Grid central de campos (3 columnas: Nombre | Tipo | PK)
    QTableWidget *fieldsTable = nullptr;
    QPushButton  *btnAddField = nullptr;
    QPushButton  *btnRemoveField = nullptr;


    int m_activeRow = -1;

    // === Helpers para controlar el único Autonumeración por tabla ===
    int        autonumRow() const;                 // índice de la fila que es Autonumeración, o -1
    QComboBox* typeComboAt(int row) const;         // combo "Tipo de datos" de la fila 'row'
    int        indexOfAutonum(QComboBox* cb) const;// índice del item "Autonumeración" en 'cb', o -1
    void       refreshAutonumLocks();              // deshabilita "Autonumeración" en combos que no son la fila auto


    // Propiedades abajo
    QTabWidget   *propTabs = nullptr;

    // General
    QComboBox   *propFormato = nullptr;      // texto "Formato" para tipos NO auto
    QComboBox    *propAutoFormato = nullptr;  // Long Integer / Replication ID (solo auto)


    QLineEdit   *propTextSize = nullptr;
    QCheckBox   *propRequerido = nullptr;

    QComboBox   *propDecimalPlaces = nullptr;   // <<--- NUEVO

    // (Opcionales, ya no se muestran)
    QComboBox    *propAutoNewValues = nullptr;
    QLineEdit    *propTitulo = nullptr;
    QComboBox    *propIndexado = nullptr;

    // helpers
    void updateGeneralUiForType(const QString& type);
    void makePropsUniformWidth();


    // Estado actual (tabla seleccionada y su esquema)
    QString m_currentTable;
    Schema  m_currentSchema;

    // helpers UI
    void setupUi();
    void applyQss();
    void loadTableToUi(const QString &tableName);
    void loadFieldPropsToUi(const FieldDef &fd);
    void pullPropsFromUi(FieldDef &fd);
    void buildRowFromField(int row, const FieldDef &fd);
    void connectRowEditors(int row);

    QString currentTableName() const;

    void clearPropsUi();
    void updateAutoControlsSensitivity();  // habilita/deshabilita "New values"
    static QString normType(const QString& t);

    // NUEVOS helpers usados por el .cpp
    void updateTablesList(const QString& preferSelect = QString());

    bool applySchemaAndRefresh(const Schema& s, int preserveRow = -1);

    // validación
    bool isValidTableName(const QString& name) const;

    bool withSidebar_ = true;
    QWidget *sidebarBox_ = nullptr;
    QTabBar *tableTabs = nullptr;

    // Guardia para evitar reentradas por cambios programáticos de UI
    bool m_updatingUi = false;
};

#endif // TABLESPAGE_H
