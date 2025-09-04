#ifndef DATAMODEL_H
#define DATAMODEL_H

#include <QObject>
#include <QMap>
#include <QVector>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QList>

/* =========================================================
 *  Definiciones existentes (compatibles con tu UI)
 * =======================================================*/
struct FieldDef {
    QString name;
    QString type;      // "Autonumeración", "Número", "Fecha/Hora", "Moneda", "Texto corto"
    int     size = 0;
    bool    pk = false;

    // Propiedades (maqueta)
    QString formato;
    QString mascaraEntrada;
    QString titulo;
    QString valorPredeterminado;
    QString reglaValidacion;
    QString textoValidacion;
    bool    requerido = false;
    QString indexado; // "No" / "Sí (con duplicados)" / "Sí (sin duplicados)"
};

// Un "Schema" es la lista de campos de una tabla (en el mismo orden que se muestran).
using Schema = QList<FieldDef>;

// Una fila de datos (mismo orden/longitud que el Schema)
using Record = QVector<QVariant>;


/* =========================================================
 *  Núcleo de datos en memoria
 *  - Guarda esquemas y datos por tabla
 *  - Valida y convierte según el tipo (string de FieldDef)
 *  - Autonumera si el PK es "Autonumeración"
 *  - Emite señales para sincronizar la UI
 * =======================================================*/
class DataModel : public QObject {
    Q_OBJECT
public:
    static DataModel& instance();

    /* ---------- Esquema ---------- */
    bool createTable(const QString& name, const Schema& s, QString* err = nullptr);
    bool dropTable(const QString& name, QString* err = nullptr);
    bool renameTable(const QString& oldName, const QString& newName, QString* err = nullptr);
    bool setSchema(const QString& name, const Schema& s, QString* err = nullptr); // ALTER (simple por nombre)

    QStringList tables() const;
    Schema schema(const QString& name) const;

    /* ---------- Datos ---------- */
    int  rowCount(const QString& name) const;
    const QVector<Record>& rows(const QString& name) const;

    bool insertRow(const QString& name, Record r, QString* err = nullptr);
    bool updateRow(const QString& name, int row, const Record& r, QString* err = nullptr);
    bool removeRows(const QString& name, const QList<int>& rows, QString* err = nullptr);

    /* ---------- Utilidades ---------- */
    bool validate(const Schema& s, Record& r, QString* err = nullptr) const; // convierte tipos in-place
    int  pkColumn(const Schema& s) const;           // -1 si no hay
    QVariant nextAutoNumber(const QString& name) const; // siguiente autonum para esa tabla
    QString tableDescription(const QString& table) const;
    void    setTableDescription(const QString& table, const QString& desc);
signals:
    void tableCreated(const QString& name);
    void tableDropped(const QString& name);
    void schemaChanged(const QString& name, const Schema& s);
    void rowsChanged(const QString& name);

private:
    explicit DataModel(QObject* parent = nullptr);
    Q_DISABLE_COPY(DataModel)

    // Normalización / validación por tipo (usa FieldDef::type)
    bool normalizeValue(const FieldDef& col, QVariant& v, QString* err) const;

    // Helpers
    bool isValidTableName(const QString& n) const;
    bool ensureUniquePk(const QString& name, int pkCol, const QVariant& pkVal,
                        int ignoreRow, QString* err) const;

private:
    QMap<QString, Schema>          m_schemas; // tabla -> schema
    QMap<QString, QVector<Record>> m_data;    // tabla -> filas
    QMap<QString, QString> m_tableDescriptions; // nombre -> descripción

};

#endif // DATAMODEL_H
