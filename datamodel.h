#ifndef DATAMODEL_H
#define DATAMODEL_H

#include <QObject>
#include <QMap>
#include <QVector>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QList>

// --- Relaciones (FK) ---
enum class FkAction { Restrict, Cascade, SetNull };


struct ForeignKey {
    QString childTable;   // tabla hija (donde vive la FK)
    int     childCol = -1;
    QString parentTable;  // tabla padre (a la que apunta)
    int     parentCol = -1;
    FkAction onDelete = FkAction::Restrict;
    FkAction onUpdate = FkAction::Restrict;
};

/* =========================================================
 *  Definiciones (compatibles con tu UI)
 * =======================================================*/
struct FieldDef {
    QString name;
    QString type;      // "Autonumeración","Número","Fecha/Hora","Moneda","Sí/No","Texto corto","Texto largo"
    int     size = 0;  // Tamaño SOLO aplica a "Texto corto"
    bool    pk = false;

    // Propiedades (maqueta)
    QString formato;
    // --- Autonumeración ---
    // Subtipo: "Long Integer" o "Replication ID" (GUID). Default Long Integer
    QString autoSubtipo = "Long Integer";
    //New values: "Increment" o "Random" (solo Long Integer). Default Increment
    QString autoNewValues = "Increment";
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
 * =======================================================*/
class DataModel : public QObject {
    Q_OBJECT
public:

    // API pública para relaciones
    bool addRelationship(const QString& childTable, const QString& childColName,
                         const QString& parentTable, const QString& parentColName,
                         FkAction onDelete = FkAction::Restrict,
                         FkAction onUpdate = FkAction::Restrict,
                         QString* err = nullptr);

    QVector<ForeignKey> relationshipsFor(const QString& table) const; // como hija
    QVector<ForeignKey> incomingRelationshipsTo(const QString& table) const;

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
    int  pkColumn(const Schema& s) const;                 // -1 si no hay
    QVariant nextAutoNumber(const QString& name) const;   // siguiente autonum
    QString tableDescription(const QString& table) const; // descripción de tabla
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

    // Por tabla hija
    QHash<QString, QVector<ForeignKey>> m_fksByChild;
    // Helpers
    int columnIndex(const Schema& s, const QString& name) const;
    bool checkFksOnWrite(const QString& childTable, const Record& r, QString* err) const;
    bool handleParentDeletes(const QString& parentTable, const QList<int>& parentRows,
                             QString* err);

private:
    QMap<QString, Schema>          m_schemas; // tabla -> schema
    QMap<QString, QVector<Record>> m_data;    // tabla -> filas
    QMap<QString, QString>         m_tableDescriptions; // nombre -> descripción
};

#endif // DATAMODEL_H
