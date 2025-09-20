#ifndef DATAMODEL_H
#define DATAMODEL_H

#include <QObject>
#include <QMap>
#include <QHash>
#include <QVector>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QList>

/* ======================== Relaciones (FK) ======================== */
enum class FkAction { Restrict, Cascade, SetNull };

struct ForeignKey {
    QString childTable;   // tabla hija (donde vive la FK)
    int     childCol = -1;
    QString parentTable;  // tabla padre (a la que apunta)
    int     parentCol = -1;
    FkAction onDelete = FkAction::Restrict;
    FkAction onUpdate = FkAction::Restrict;
};

/* =================== Definiciones (compatibles UI) =================== */
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
    // New values: "Increment" o "Random" (solo Long Integer). Default Increment
    QString autoNewValues = "Increment";

    QString mascaraEntrada;
    QString titulo;
    QString reglaValidacion;
    QString textoValidacion;

    bool    requerido = false;
    QString indexado; // "No" / "Sí (con duplicados)" / "Sí (sin duplicados)"
};

// Un "Schema" es la lista de campos de una tabla (en el mismo orden que se muestran).
using Schema = QList<FieldDef>;
// Una fila de datos (mismo orden/longitud que el Schema)
using Record = QVector<QVariant>;




/* ========================= Núcleo de datos ========================= */
class DataModel : public QObject {
    Q_OBJECT
public:
    static DataModel& instance();
    void markColumnEdited(const QString& table, const QString& colName);


    QVariant nextAutoNumber(const QString& name);  // ← sin const

    int autoColumn(const Schema& s) const;



    /* ---------- Persistencia ---------- */
    bool loadFromJson(const QString& file, QString* err = nullptr);
    bool saveToJson(const QString& file, QString* err = nullptr) const;

    /* ---------- Relaciones ---------- */
    bool addRelationship(const QString& childTable, const QString& childColName,
                         const QString& parentTable, const QString& parentColName,
                         FkAction onDelete = FkAction::Restrict,
                         FkAction onUpdate = FkAction::Restrict,
                         QString* err = nullptr);

    QVector<ForeignKey> relationshipsFor(const QString& table) const; // como hija
    QVector<ForeignKey> incomingRelationshipsTo(const QString& table) const;

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

    // NOTA: estas firmas se mantienen para no romper nada;
    // internamente usarán Avail List (reutilización de huecos + tombstones)
    bool insertRow(const QString& name, Record r, QString* err = nullptr);
    bool updateRow(const QString& name, int row, const Record& r, QString* err = nullptr);
    bool removeRows(const QString& name, const QList<int>& rows, QString* err = nullptr);

    /* ---------- Utilidades ---------- */
    // Convierte/normaliza tipos in-place y valida requeridos/tamaños (se mantiene tu firma)
    bool validate(const Schema& s, Record& r, QString* err = nullptr) const;

    int       pkColumn(const Schema& s) const;               // -1 si no hay
    QVariant  nextAutoNumber(const QString& name) const;     // siguiente autonum
    QString   tableDescription(const QString& table) const;  // descripción de tabla
    void      setTableDescription(const QString& table, const QString& desc);

    /* ---------- Avail List / Compact / Métricas ---------- */
    // Compacta la tabla eliminando tombstones y limpiando la free list. Devuelve cuántos huecos se removieron.
    int compactTable(const QString& table, QString* err = nullptr);

    struct AvailStats {
        int total{0};     // tamaño del vector interno (incluye tombstones)
        int deleted{0};   // filas marcadas como tombstone
        int freeSlots{0}; // posiciones disponibles en la free list
    };
    AvailStats availStats(const QString& table) const;

signals:
    void tableCreated(const QString& name);
    void tableDropped(const QString& name);
    void schemaChanged(const QString& name, const Schema& s);
    void rowsChanged(const QString& name);

private:
    explicit DataModel(QObject* parent = nullptr);
    Q_DISABLE_COPY(DataModel)

    // Baseline de valores cuando una columna salió de Autonumeración
    QHash<QString, QHash<QString, QVector<QVariant>>> m_autoBaseline;

    // Conjunto de columnas que fueron AUTONUM y han sido EDITADAS desde que salieron
    QHash<QString, QSet<QString>> m_autoEditedSinceLeave;

    /* ---------------- Normalización / validación por tipo ---------------- */
    // Normaliza/convierte un valor según FieldDef::type (usada por validate)
    bool normalizeValue(const FieldDef& col, QVariant& v, QString* err) const;

    // Último ID emitido por tabla (no disminuye cuando borras)
    QHash<QString, qint64> m_lastIssuedId;

    // Inicializa el contador al máximo actual de la tabla si aún no está
    void ensureAutoCounterInitialized(const QString& table);

    // Normaliza etiqueta de tipo (igual que en UI): "numero","moneda","fecha_hora", etc.
    QString normType(const QString& t) const;

    /* ----------------- Helpers comunes de validación ----------------- */
    // Índice de campo por nombre (en Schema o por tabla)
    int  columnIndex(const Schema& s, const QString& name) const; // ya existente
    int  fieldIndex(const Schema& s, const QString& name) const;  // alias explícito
    int  fieldIndex(const QString& table, const QString& name) const;

    // ¿Campo es único? (PK o índice "sin duplicados")
    bool isUniqueField(const FieldDef& f) const;

    // Comparación tolerante (útil para double / nulos)
    bool sameValue(const QVariant& a, const QVariant& b) const;

    // Validación central de registro (tipos, tamaños, requeridos)
    bool validateRecordCore(const Schema& s, const Record& r, QString* err) const;

    // Verifica unicidad (PK/unique) respecto a los datos actuales
    bool checkUniqueness(const QString& table, const Schema& s,
                         const Record& candidate, int skipRow, QString* err) const;

    // Verifica FKs del registro (child → parent existente si no es NULL)
    bool checkForeignKeys(const QString& table, const Schema& s,
                          const Record& candidate, QString* err) const;

    // Autonumeración: asigna MAX+1 si el valor viene vacío
    void assignAutonumberIfNeeded(const QString& table, const Schema& s, Record& r);

    /* ----------------- Otros helpers existentes ----------------- */
    bool isValidTableName(const QString& n) const;
    bool ensureUniquePk(const QString& name, int pkCol, const QVariant& pkVal,
                        int ignoreRow, QString* err) const;

    // Por tabla hija
    QHash<QString, QVector<ForeignKey>> m_fksByChild;

    // Verifica FKs en operaciones de escritura (interno clásico)
    bool checkFksOnWrite(const QString& childTable, const Record& r, QString* err) const;

    // Maneja cascadas/bloqueos ante eliminación de padres (según onDelete)
    bool handleParentDeletes(const QString& parentTable, const QList<int>& parentRows,
                             QString* err);

    /* ----------------- Avail List internals ----------------- */
    // Free list por tabla: contiene índices de filas "tombstone" reutilizables (LIFO).
    QMap<QString, QVector<int>> m_freeList;

    // Consideramos "tombstone" a un Record vacío -> hueco reutilizable en Avail List.
    static inline bool isTombstone(const Record& r) { return r.isEmpty(); }

    // Asegura que existan entradas para tabla en mapas internos (schemas, data, freeList).
    void ensureTableExists(const QString& table);

private:
    QMap<QString, Schema>          m_schemas; // tabla -> schema
    QMap<QString, QVector<Record>> m_data;    // tabla -> filas (incluye tombstones)
    QMap<QString, QString>         m_tableDescriptions; // nombre -> descripción
};

#endif // DATAMODEL_H
