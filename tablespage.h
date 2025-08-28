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

struct FieldDef {
    QString name;
    QString type;      // Autonumeración, Número, Fecha/Hora, Moneda, Texto corto
    int     size = 0;  // tamaño/precisión sugerido
    bool    pk = false;

    // Propiedades (maqueta)
    QString formato;
    QString mascaraEntrada;
    QString titulo;
    QString valorPredeterminado;
    QString reglaValidacion;
    QString textoValidacion;
    bool    requerido = false;
    QString indexado; // No / Sí (con duplicados) / Sí (sin duplicados)
};

class TablesPage : public QWidget {
    Q_OBJECT
public:
    explicit TablesPage(QWidget *parent = nullptr, bool withSidebar = true);
    QListWidget* tableListWidget() const { return tablesList; }


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
    QListWidget  *tablesList;

    // Barra superior
    QLineEdit    *tableNameEdit;
    QLineEdit    *tableDescEdit;     // NUEVO: descripción de tabla
    QPushButton  *btnNueva;
    QPushButton  *btnEditar;
    QPushButton  *btnEliminar;

    // Grid central de campos
    QTableWidget *fieldsTable;
    QPushButton  *btnAddField;
    QPushButton  *btnRemoveField;

    // Propiedades abajo
    QTabWidget   *propTabs;

    // General
    QLineEdit    *propFormato;
    QLineEdit    *propMascara;
    QLineEdit    *propTitulo;
    QLineEdit    *propValorPred;
    QLineEdit    *propReglaVal;
    QLineEdit    *propTextoVal;
    QCheckBox    *propRequerido;
    QComboBox    *propIndexado;

    // Datos en memoria: nombreTabla -> lista de campos
    QMap<QString, QList<FieldDef>> dbMock;

    // NUEVO: memoria de descripciones por tabla
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

    // NUEVO: helper para limpiar panel de propiedades
    void clearPropsUi();

    // helpers de validación/consulta
    bool tableExists(const QString& name) const;
    bool isValidTableName(const QString& name) const;

    bool withSidebar_ = true;
    QWidget *sidebarBox_ = nullptr;
};

#endif // TABLESPAGE_H
