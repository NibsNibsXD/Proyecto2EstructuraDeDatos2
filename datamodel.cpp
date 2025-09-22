#include "datamodel.h"

#include <QtGlobal>
#include <QRegularExpression>
#include <QDate>
#include <QSet>
#include <QHash>
#include <algorithm>
#include <QUuid>
#include <QRandomGenerator>
#include <climits>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <QLocale>

/* ====================== Singleton ====================== */

DataModel& DataModel::instance() {
    static DataModel inst;
    return inst;
}

int DataModel::autoColumn(const Schema& s) const {
    for (int i = 0; i < s.size(); ++i)
        if (normType(s[i].type) == "autonumeracion")
            return i;
    return -1;
}

DataModel::DataModel(QObject* parent) : QObject(parent) {}

/* ====================== Helpers (libres) ====================== */

static inline bool isEmptyVar(const QVariant& v) {
    if (!v.isValid() || v.isNull()) return true;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    if (v.typeId() == QMetaType::QString)
        return v.toString().trimmed().isEmpty();
#else
    if (v.type() == QVariant::String)
        return v.toString().trimmed().isEmpty();
#endif
    return false;
}

// Mapea el texto de FieldDef.type a una etiqueta estable
static inline QString normType_free(const QString& t) {
    const QString s = t.trimmed().toLower();
    if (s.startsWith(u"auto"))                                    return "autonumeracion";
    if (s.startsWith(u"número") || s.startsWith(u"numero"))       return "numero";
    if (s.startsWith(u"fecha"))                                   return "fecha_hora";
    if (s.startsWith(u"moneda"))                                  return "moneda";
    if (s.startsWith(u"sí/no") || s.startsWith(u"si/no"))         return "booleano";
    if (s.startsWith(u"texto largo"))                             return "texto_largo";
    return "texto"; // por defecto (Texto corto)
}

/* ====================== Métodos privados (cabecera) ====================== */

QString DataModel::normType(const QString& t) const {
    return normType_free(t);
}

bool DataModel::isValidTableName(const QString& n) const {
    static const QRegularExpression rx("^[A-Za-z_][A-Za-z0-9_]*$");
    return rx.match(n).hasMatch();
}

int DataModel::pkColumn(const Schema& s) const {
    for (int i = 0; i < s.size(); ++i)
        if (s[i].pk) return i;
    return -1;
}

int DataModel::fieldIndex(const Schema& s, const QString& name) const {
    for (int i = 0; i < s.size(); ++i)
        if (QString::compare(s[i].name, name, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

int DataModel::fieldIndex(const QString& table, const QString& name) const {
    return fieldIndex(m_schemas.value(table), name);
}

bool DataModel::isUniqueField(const FieldDef& f) const {
    if (f.pk) return true;
    return f.indexado.contains("sin duplicados", Qt::CaseInsensitive);
}

bool DataModel::sameValue(const QVariant& a, const QVariant& b) const {
    if (!a.isValid() && !b.isValid()) return true;
    if (a.isNull() && b.isNull()) return true;
    if (a.typeName() == b.typeName()) return a == b;

// Comparación tolerante:
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    if (a.canConvert<double>() && b.canConvert<double>()) {
#else
    if (a.canConvert(QMetaType::Double) && b.canConvert(QMetaType::Double)) {
#endif
        return std::fabs(a.toDouble() - b.toDouble()) < 1e-9;
    }
    return a.toString() == b.toString();
}

bool DataModel::validateRecordCore(const Schema& s, const Record& r, QString* err) const {
    if (r.size() < s.size()) {
        if (err) *err = tr("Registro con longitud inválida.");
        return false;
    }
    // Requeridos y tamaños se validan en normalizeValue (que usa validate)
    return true;
}

bool DataModel::checkUniqueness(const QString& table, const Schema& s,
                                const Record& candidate, int skipRow, QString* err) const
{
    const auto& vec = m_data.value(table);
    // 1) PK
    const int pk = pkColumn(s);
    if (pk >= 0 && pk < candidate.size()) {
        const QVariant& pkVal = candidate[pk];
        if (pkVal.isValid() && !pkVal.isNull()) {
            for (int i = 0; i < vec.size(); ++i) {
                if (i == skipRow) continue;
                const Record& rec = vec[i];
                if (rec.isEmpty()) continue;
                if (pk < rec.size() && sameValue(rec[pk], pkVal)) {
                    if (err) *err = tr("Clave primaria duplicada: %1").arg(pkVal.toString());
                    return false;
                }
            }
        }
    }
    // 2) Únicos ("Sí (sin duplicados)")
    for (int c = 0; c < s.size(); ++c) {
        const FieldDef& f = s[c];
        if (!isUniqueField(f)) continue;
        const QVariant& val = candidate.value(c);
        if (!val.isValid() || val.isNull()) continue; // NULLs permiten duplicados típicamente
        for (int i = 0; i < vec.size(); ++i) {
            if (i == skipRow) continue;
            const Record& rec = vec[i];
            if (rec.isEmpty()) continue;
            if (c < rec.size() && sameValue(rec[c], val)) {
                if (f.pk) {
                    if (err) *err = tr("Clave primaria duplicada: %1").arg(val.toString());
                } else {
                    if (err) *err = tr("Valor duplicado en campo único \"%1\": %2").arg(f.name, val.toString());
                }
                return false;
            }
        }
    }
    return true;
}

bool DataModel::checkForeignKeys(const QString& table, const Schema& s,
                                 const Record& candidate, QString* err) const
{
    Q_UNUSED(s);
    return checkFksOnWrite(table, candidate, err);
}

void DataModel::assignAutonumberIfNeeded(const QString& table, const Schema& s, Record& r) {
    const int ac = autoColumn(s);                   // ← en vez de pkColumn(s)
    if (ac >= 0) {
        r.resize(std::max(r.size(), s.size()));
        if (isEmptyVar(r[ac])) {
            r[ac] = nextAutoNumber(table);          // ← se autollenará igual
        }
    }
}

void DataModel::ensureAutoCounterInitialized(const QString& table)
{
    if (m_lastIssuedId.contains(table)) return;

    const auto& rows = this->rows(table);
    const Schema sch = this->schema(table);

    // localizar columna de Autonumeración (aunque no sea PK)
    const int ac = autoColumn(sch);
    if (ac < 0) {                      // no hay autonum en esta tabla
        m_lastIssuedId.insert(table, 0);
        return;
    }

    qint64 maxId = 0;
    for (const Record& rec : rows) {
        if (rec.isEmpty() || ac >= rec.size()) continue;
        bool ok = false;
        const qint64 v = rec[ac].toLongLong(&ok);
        if (ok && v > maxId) maxId = v;
    }
    m_lastIssuedId.insert(table, maxId);   // arranca en el máximo existente
}

QVariant DataModel::nextAutoNumber(const QString& name)
{
    const Schema sch = m_schemas.value(name);

    // ← localizar la columna AUTONUM (aunque no sea PK)
    const int ac = autoColumn(sch);
    if (ac < 0) return static_cast<qint64>(1);

    const FieldDef& fd = sch[ac];
    auto it = m_data.constFind(name);

    // 1) Replication ID -> GUID
    if (normType(fd.type) == "autonumeracion" &&
        fd.autoSubtipo.trimmed().toLower().startsWith("replication"))
    {
        return QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    // 2) Random entero único
    if (fd.autoNewValues.toLower().startsWith("random")) {
        if (it == m_data.constEnd() || it->isEmpty()) {
            return static_cast<qint64>(QRandomGenerator::global()->bounded(1, INT_MAX));
        }
        while (true) {
            const qint64 v = static_cast<qint64>(QRandomGenerator::global()->bounded(1, INT_MAX));
            bool clash = false;
            for (const auto& rec : it.value()) {
                if (rec.isEmpty()) continue;                 // omite tombstones
                if (ac < rec.size() && rec.value(ac).toLongLong() == v) { clash = true; break; }
            }
            if (!clash) return v;
        }
    }

    // 3) Incremental MONÓTONO: último emitido + 1
    ensureAutoCounterInitialized(name);                      // ya usa autoColumn internamente
    return QVariant(m_lastIssuedId.value(name, 0) + 1);
}

bool DataModel::normalizeValue(const FieldDef& col, QVariant& v, QString* err) const {

    const QString t = normType(col.type);
    if (isEmptyVar(v)) {
        // Autonumeración: permitir vacío (lo llenará assignAutonumberIfNeeded / nextAutoNumber)
        if (t == "autonumeracion") {
            v = QVariant();
            return true;
        }
        // Para los demás tipos: si es requerido (y no PK), no puede estar vacío
        if (col.requerido && !col.pk) {
            if (err) *err = tr("El campo \"%1\" es requerido.").arg(col.name);
            return false;
        }
        v = QVariant(); // null aceptado cuando no es requerido
        return true;
    }

    if (t == "autonumeracion") {
        // Validación depende del subtipo
        if (col.autoSubtipo.trimmed().toLower().startsWith("replication")) {
            // Acepta string (GUID); si no es string, conviértelo a string
            v = v.toString();
            return true;
        }
        // Long Integer: debe ser entero si trae valor
        bool ok = false;
        v.toLongLong(&ok);
        if (!ok) { if (err) *err = tr("El campo \"%1\" debe ser entero.").arg(col.name); return false; }
        v = v.toLongLong();
        return true;
    }

    if (t == "numero") {
        const QString sz = col.autoSubtipo.trimmed().toLower();
        const bool isInt =
            sz.contains("byte") || sz.contains("entero") || sz.contains("integer") || sz.contains("long");

        if (isInt) {
            bool okD = false;
            const double d = v.toDouble(&okD);
            if (!okD) { if (err) *err = tr("El campo \"%1\" debe ser entero/numérico.").arg(col.name); return false; }
            v = static_cast<qint64>(std::llround(d));
            return true;
        } else {
            bool ok = false;
            const double d = v.toDouble(&ok);
            if (!ok) { if (err) *err = tr("El campo \"%1\" debe ser numérico.").arg(col.name); return false; }
            v = d;
            return true;
        }
    }

    if (t == "moneda") {
        bool ok = false; double d = v.toDouble(&ok);
        if (!ok) { if (err) *err = tr("El campo \"%1\" debe ser moneda (número).").arg(col.name); return false; }
        v = d;
        return true;
    }

    if (t == "fecha_hora") {
        // Ya contemplas QDate directo:
        if (v.canConvert<QDate>()) {
            QDate d = v.toDate();
            if (!d.isValid()) { if (err) *err = tr("Fecha inválida en \"%1\".").arg(col.name); return false; }
            v = d; return true;
        }

        const QString s = v.toString().trimmed();
        QDate d;

        // 1) Formatos del diseñador
        d = QDate::fromString(s, "dd-MM-yy");
        if (!d.isValid()) d = QDate::fromString(s, "dd/MM/yy");

        // 2) Mes en texto español
        if (!d.isValid()) {
            QLocale es(QLocale::Spanish, QLocale::Honduras);
            d = es.toDate(s, "dd/MMMM/yyyy"); // ej: 05/enero/2025
        }

        // 3) Back-compat
        if (!d.isValid()) d = QDate::fromString(s, "yyyy-MM-dd");
        if (!d.isValid()) d = QDate::fromString(s, "dd/MM/yyyy");
        if (!d.isValid()) d = QDate::fromString(s, "dd-MM-yyyy");
        if (!d.isValid()) d = QDate::fromString(s, "MM/dd/yyyy");

        if (!d.isValid()) {
            if (err) *err = tr("Fecha inválida en \"%1\". Use DD/MM/YY, DD-MM-YY o DD/MESTEXTO/YYYY.").arg(col.name);
            return false;
        }
        v = d;
        return true;
    }

    if (t == "booleano") {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        if (v.typeId() == QMetaType::Bool)
#else
        if (v.type() == QVariant::Bool)
#endif
        { v = v.toBool(); return true; }

        const QString s = v.toString().trimmed().toLower();
        if (s=="1" || s=="true" || s=="sí" || s=="si" || s=="yes") { v = true;  return true; }
        if (s=="0" || s=="false"|| s=="no")                         { v = false; return true; }
        if (err) *err = tr("El campo \"%1\" debe ser Sí/No.").arg(col.name);
        return false;
    }

    if (t == "texto_largo") {
        v = v.toString(); // sin límite
        return true;
    }

    // Texto corto
    {
        QString s = v.toString();
        if (col.size > 0 && s.size() > col.size) s.truncate(col.size);

        v = s;
        return true;
    }
}

bool DataModel::ensureUniquePk(const QString& name, int pkCol, const QVariant& pkVal,
                               int ignoreRow, QString* err) const
{
    if (pkCol < 0) return true;
    const auto& vec = m_data.value(name);
    for (int i = 0; i < vec.size(); ++i) {
        if (i == ignoreRow) continue;
        const Record& rec = vec[i];
        if (rec.isEmpty()) continue; // ⟵ omitir tombstones
        const QVariant& v = rec.value(pkCol);
        if (v.isValid() && !v.isNull() && v == pkVal) {
            if (err) *err = tr("Clave primaria duplicada: %1").arg(pkVal.toString());
            return false;
        }
    }
    return true;
}

/* ====================== Esquema ====================== */

QStringList DataModel::tables() const {
    QStringList t = m_schemas.keys();
    t.sort(Qt::CaseInsensitive);
    return t;
}

Schema DataModel::schema(const QString& name) const {
    return m_schemas.value(name);
}

QString DataModel::tableDescription(const QString& table) const {
    return m_tableDescriptions.value(table);
}

void DataModel::setTableDescription(const QString& table, const QString& desc) {
    if (table.isEmpty()) return;
    if (!m_schemas.contains(table)) return;
    m_tableDescriptions[table] = desc;
    // opcional: emit schemaChanged(table, m_schemas.value(table));
}

void DataModel::ensureTableExists(const QString& table) {
    if (!m_schemas.contains(table)) m_schemas[table] = Schema{};
    if (!m_data.contains(table))    m_data[table]    = {};
    if (!m_freeList.contains(table))m_freeList[table]= {};
}

bool DataModel::createTable(const QString& name, const Schema& s, QString* err) {
    if (!isValidTableName(name)) { if (err) *err = tr("Nombre de tabla inválido: %1").arg(name); return false; }
    if (m_schemas.contains(name)) { if (err) *err = tr("La tabla ya existe: %1").arg(name); return false; }

    // Aceptar esquema vacío al crear; el usuario lo diseñará luego.
    Schema s2 = s;  // ← trabajamos sobre una copia editable para poder forzar 'requerido'
    if (!s2.isEmpty()) {
        QSet<QString> names;
        int pkCount   = 0;
        int autoCount = 0;
        int autoIdx   = -1;

        for (int i = 0; i < s2.size(); ++i) {
            const auto& c = s2[i];
            if (c.name.trimmed().isEmpty()) { if (err) *err = tr("Columna sin nombre."); return false; }
            if (names.contains(c.name))      { if (err) *err = tr("Columna duplicada: %1").arg(c.name); return false; }
            names.insert(c.name);

            if (c.pk) ++pkCount;
            if (normType(c.type) == "autonumeracion") {
                ++autoCount;
                autoIdx = i;
            }
        }

        if (pkCount > 1)    { if (err) *err = tr("Solo se permite una PK."); return false; }
        if (autoCount > 1)  { if (err) *err = tr("Solo se permite un campo de Autonumeración por tabla."); return false; }
        if (autoIdx >= 0) {
            s2[autoIdx].requerido = true; // ← Autonumeración SIEMPRE requerido
        }
    }

    m_schemas.insert(name, s2);
    m_data.insert(name, {});
    m_tableDescriptions.insert(name, QString());
    m_freeList.insert(name, {}); // ⟵ preparar avail list
    emit tableCreated(name);
    emit schemaChanged(name, s2);
    return true;
}

bool DataModel::dropTable(const QString& name, QString* err) {
    if (!m_schemas.contains(name)) { if (err) *err = tr("No existe la tabla: %1").arg(name); return false; }
    m_schemas.remove(name);
    m_data.remove(name);
    m_tableDescriptions.remove(name);
    m_freeList.remove(name);
    emit tableDropped(name);
    return true;
}

bool DataModel::renameTable(const QString& oldName, const QString& newName, QString* err) {
    if (!m_schemas.contains(oldName)) { if (err) *err = tr("No existe: %1").arg(oldName); return false; }
    if (!isValidTableName(newName))   { if (err) *err = tr("Nombre inválido: %1").arg(newName); return false; }
    if (m_schemas.contains(newName))  { if (err) *err = tr("Ya existe: %1").arg(newName); return false; }

    m_schemas.insert(newName, m_schemas.take(oldName));
    m_data.insert(newName,   m_data.take(oldName));
    m_tableDescriptions.insert(newName, m_tableDescriptions.take(oldName));
    m_freeList.insert(newName, m_freeList.take(oldName));
    emit tableDropped(oldName);
    emit tableCreated(newName);
    emit schemaChanged(newName, m_schemas.value(newName));
    return true;
}

void DataModel::markColumnEdited(const QString& table, const QString& colName)
{
    // Solo marca si esa columna salió antes de Autonumeración (hay baseline guardado)
    if (m_autoBaseline.contains(table) && m_autoBaseline[table].contains(colName)) {
        m_autoEditedSinceLeave[table].insert(colName);
    }
}

bool DataModel::setSchema(const QString& name, const Schema& s, QString* err) {
    if (!m_schemas.contains(name)) { if (err) *err = tr("No existe la tabla: %1").arg(name); return false; }

    // --- VALIDACIÓN: duplicados, PK única, y SOLO 1 Autonumeración (si hay, forzar requerido=true)
    Schema s2 = s; // copia editable
    {
        QSet<QString> names;
        int pkCount   = 0;
        int autoCount = 0;
        int autoIdx   = -1;

        for (int i = 0; i < s2.size(); ++i) {
            const auto& c = s2[i];
            if (c.name.trimmed().isEmpty()) { if (err) *err = tr("Columna sin nombre."); return false; }
            if (names.contains(c.name))      { if (err) *err = tr("Columna duplicada: %1").arg(c.name); return false; }
            names.insert(c.name);

            if (c.pk) ++pkCount;
            if (normType(c.type) == "autonumeracion") { ++autoCount; autoIdx = i; }
        }
        if (pkCount > 1)   { if (err) *err = tr("Solo se permite una PK."); return false; }
        if (autoCount > 1) { if (err) *err = tr("Solo se permite un campo de Autonumeración por tabla."); return false; }
        if (autoIdx >= 0) {
            s2[autoIdx].requerido = true; // Autonumeración SIEMPRE requerido
        }
    }

    // Migración sencilla por nombre + soporte a rename, usando s2 (no s)
    const Schema oldS  = m_schemas.value(name);
    const auto   oldRs = m_data.value(name);

    QHash<QString,int> oldIndex;
    for (int i = 0; i < oldS.size(); ++i) oldIndex.insert(oldS[i].name, i);

    // --- mapa robusto de columnas nuevas -> viejas (con soporte a rename)
    QVector<int>  mapNewToOld(s2.size(), -1);
    QVector<bool> oldUsed(oldS.size(), false);

    // 1) Emparejar por nombre
    for (int i = 0; i < s2.size(); ++i) {
        int oi = oldIndex.value(s2[i].name, -1);
        if (oi >= 0) { mapNewToOld[i] = oi; oldUsed[oi] = true; }
    }

    // 2) Detectar "nuevas" (renombradas realmente) y "viejas" sin usar
    QVector<int> newMissing, oldMissing;
    for (int i = 0; i < s2.size(); ++i) if (mapNewToOld[i] < 0) newMissing.append(i);
    for (int oi = 0; oi < oldS.size(); ++oi) if (!oldUsed[oi]) oldMissing.append(oi);

    // 3) Si la cuenta coincide, asumimos RENAME(s). Emparejar por tipo/PK, con fallback por orden
    if (!newMissing.isEmpty() && newMissing.size() == oldMissing.size()) {
        auto typeKey = [](const FieldDef& fd) {
            return QString("%1|%2").arg(
                QString(fd.type).trimmed().toLower(),
                fd.pk ? "pk" : "nopk"
                );
        };

        QSet<int> taken;
        for (int ni : newMissing) {
            int chosen = -1;

            // a) por tipo/PK
            for (int oi : oldMissing) {
                if (taken.contains(oi)) continue;
                if (typeKey(s2[ni]) == typeKey(oldS[oi])) { chosen = oi; break; }
            }
            // b) fallback: primero libre
            if (chosen == -1) {
                for (int oi : oldMissing) { if (!taken.contains(oi)) { chosen = oi; break; } }
            }

            if (chosen != -1) {
                mapNewToOld[ni] = chosen;
                taken.insert(chosen);
            }
        }
    }

    // 4) Construir filas nuevas usando el mapeo final (conserva datos en renames)
    QVector<Record> newRows;
    newRows.reserve(oldRs.size());

    for (const auto& r : oldRs) {
        if (r.isEmpty()) { newRows.append(Record{}); continue; } // conservar tombstones
        Record nr(s2.size());
        for (int i = 0; i < s2.size(); ++i) {
            const FieldDef& col = s2[i];
            const int oi = mapNewToOld[i];
            QVariant v = (oi >= 0 && oi < r.size()) ? r[oi] : QVariant();

            QString convErr;
            if (!normalizeValue(col, v, &convErr)) {
                v = QVariant(); // si no convierte, lo dejamos nulo
            }
            nr[i] = v;
        }
        newRows.append(nr);
    }

    // ===== Reglas de PK: no bool, sin NULL/vacíos, sin duplicados =====
    // (validamos contra s2 + newRows ya normalizados)
    int pkCol = -1;
    for (int i = 0; i < s2.size(); ++i) {
        if (s2[i].pk) { pkCol = i; break; }
    }
    if (pkCol >= 0) {
        // Forzar requerido en PK
        s2[pkCol].requerido = true;

        const QString ntype = normType(s2[pkCol].type);
        const bool textPk   = (ntype == "texto" || ntype == "texto_largo");

        // 0) Tipo prohibido: booleano jamás puede ser PK
        if (ntype == "booleano") {
            if (err) *err = tr("El tipo Sí/No (Booleano) no puede ser llave primaria.");
            return false;
        }

        // 1) Vacíos/NULLs no permitidos en datos existentes
        for (const Record& rec : newRows) {
            if (rec.isEmpty()) continue;                  // tombstone
            if (pkCol >= rec.size()) continue;
            const QVariant& v = rec[pkCol];
            const bool isEmpty =
                !v.isValid() || v.isNull()
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
                || (v.typeId() == QMetaType::QString && v.toString().trimmed().isEmpty());
#else
                || (v.type() == QVariant::String && v.toString().trimmed().isEmpty());
#endif
            if (isEmpty) {
                if (err) *err = tr("La columna \"%1\" contiene valores vacíos. No puede ser llave primaria.")
                               .arg(s2[pkCol].name);
                return false;
            }
        }

        // 2) Duplicados no permitidos (texto: case-insensitive)
        QSet<QString> seen;
        auto keyFor = [&](const QVariant& v)->QString {
            // ya filtramos vacíos; aquí solo normalizamos la clave
            if (textPk) return v.toString().trimmed().toLower();  // sin distinguir mayúsc/minúsc
            return v.toString();
        };

        for (const Record& rec : newRows) {
            if (rec.isEmpty() || pkCol >= rec.size()) continue;
            const QString k = keyFor(rec[pkCol]);
            if (seen.contains(k)) {
                if (textPk) {
                    if (err) *err = tr("La columna \"%1\" tiene valores duplicados (sin distinguir mayús/minús). "
                                  "No puede ser llave primaria.").arg(s2[pkCol].name);
                } else {
                    if (err) *err = tr("La columna \"%1\" tiene valores duplicados. No puede ser llave primaria.")
                                   .arg(s2[pkCol].name);
                }
                return false;
            }
            seen.insert(k);
        }
    }

    // A) Si una columna deja de ser Autonumeración, guardar baseline y limpiar flag de "editada"
    {
        auto& tblBaseline = m_autoBaseline[name]; // crea entrada si no existe
        for (int newCol = 0; newCol < s2.size(); ++newCol) {
            const int oldCol = mapNewToOld.value(newCol, -1);
            if (oldCol < 0) continue; // columna nueva: no aplica

            const bool wasAuto = (normType(oldS[oldCol].type) == "autonumeracion");
            const bool toAuto  = (normType(s2[newCol].type)   == "autonumeracion");

            if (wasAuto && !toAuto) {
                QVector<QVariant> snap;
                snap.reserve(oldRs.size());
                for (const Record& rec : oldRs) {
                    snap.push_back(oldCol < rec.size() ? rec[oldCol] : QVariant());
                }
                const QString colNameNow = s2[newCol].name; // nombre actual
                tblBaseline[colNameNow] = std::move(snap);
                m_autoEditedSinceLeave[name].remove(colNameNow); // aún no ha sido editada
            }
        }
    }

    // B) Regla: si la columna fue editada DESPUÉS de salir de Autonumeración, no puede volver a Autonumeración
    for (int newCol = 0; newCol < s2.size(); ++newCol) {
        const bool toAuto = (normType(s2[newCol].type) == "autonumeracion");
        if (!toAuto) continue;

        const int oldCol   = mapNewToOld.value(newCol, -1);
        const bool wasAuto = (oldCol >= 0 && normType(oldS[oldCol].type) == "autonumeracion");
        if (wasAuto) continue; // seguía siendo autonum; no es "volver"

        const QString colNameNow = s2[newCol].name;

        // 1) Si está marcada como editada tras salir → veto directo
        if (m_autoEditedSinceLeave[name].contains(colNameNow)) {
            if (err) *err = tr("No puede volver a \"Autonumeración\": la columna fue editada después de salir de Autonumeración.");
            return false;
        }

        // 2) (respaldo) Si hay baseline y el contenido actual difiere del baseline → veto
        if (m_autoBaseline.contains(name) && m_autoBaseline[name].contains(colNameNow)) {
            const QVector<QVariant>& snap = m_autoBaseline[name][colNameNow];
            bool differs = false;

            const QVector<Record>& rows = m_data.value(name);
            const int N = std::max(snap.size(), rows.size());
            for (int r = 0; r < N; ++r) {
                QVariant cur;
                if (r < rows.size()) {
                    const Record& rec = rows[r];
                    if (newCol < rec.size()) cur = rec[newCol];
                }
                const QVariant base = (r < snap.size() ? snap[r] : QVariant());
                if (!sameValue(cur, base)) { differs = true; break; }
            }
            if (differs) {
                if (err) *err = tr("No puede volver a \"Autonumeración\": la columna fue editada después de salir de Autonumeración.");
                return false;
            }
        }
    }

    // Sustituir esquema y datos (usar s2, no s)
    m_schemas[name] = s2;
    m_data[name]    = newRows;

    // Recalcular free list (tombstones) tras cambio de esquema
    m_freeList[name].clear();
    for (int i = 0; i < m_data[name].size(); ++i)
        if (m_data[name][i].isEmpty()) m_freeList[name].push_back(i);

    // Recalcular contador SÓLO si cambió cuál columna es Autonumeración
    const int oldAuto = autoColumn(oldS);
    const int newAuto = autoColumn(s2);
    if (oldAuto != newAuto) {
        m_lastIssuedId.remove(name);        // se recalculará con ensureAutoCounterInitialized
    }
    emit schemaChanged(name, s2);
    return true;
}

/* ====================== Datos ====================== */

int DataModel::rowCount(const QString& name) const {
    return m_data.value(name).size();
}

const QVector<Record>& DataModel::rows(const QString& name) const {
    static const QVector<Record> kEmpty;
    auto it = m_data.constFind(name);
    if (it == m_data.constEnd()) return kEmpty;
    return it.value();
}

bool DataModel::validate(const Schema& s, Record& r, QString* err) const {
    if (r.size() < s.size()) r.resize(s.size());
    for (int i = 0; i < s.size(); ++i) {
        QVariant v = r[i];
        if (!normalizeValue(s[i], v, err)) return false;
        r[i] = v;
    }
    return true;
}

bool DataModel::insertRow(const QString& name, Record r, QString* err) {
    if (!m_schemas.contains(name)) { if (err) *err = tr("No existe la tabla: %1").arg(name); return false; }
    ensureTableExists(name);
    const Schema s = m_schemas.value(name);

    // Autonum si aplica y viene vacío
    assignAutonumberIfNeeded(name, s, r);

    // Normalización + requeridos
    if (!validate(s, r, err)) return false;

    // Integridad referencial
    if (!checkForeignKeys(name, s, r, err)) return false;

    // Unicidad (PK + únicos)
    if (!checkUniqueness(name, s, r, -1, err)) return false;

    // === Avail List: reutiliza huecos antes de hacer append ===
    auto& vec  = m_data[name];

    auto& free = m_freeList[name];

    if (!free.isEmpty()) {
        int idx = free.back();
        free.pop_back();
        if (idx >= 0 && idx < vec.size() && vec[idx].isEmpty()) {
            vec[idx] = r;               // reutilización in-place
        } else {
            vec.push_back(r);           // fallback seguro
        }
    } else {
        vec.push_back(r);
    }

    // ← AVANZAR contador monótono si la tabla tiene columna de Autonumeración (sea o no PK)
    {
        const int ac = autoColumn(s);
        if (ac >= 0) {
            ensureAutoCounterInitialized(name);
            bool ok = false;
            const qint64 v = (ac < r.size() ? r[ac].toLongLong(&ok) : 0);
            if (ok && v > m_lastIssuedId.value(name, 0)) {
                m_lastIssuedId[name] = v;
            }
        }
    }

    emit rowsChanged(name);
    return true;
}

bool DataModel::updateRow(const QString& name, int row, const Record& newR, QString* err) {
    if (!m_schemas.contains(name)) { if (err) *err = tr("No existe la tabla: %1").arg(name); return false; }
    ensureTableExists(name);
    auto& vec = m_data[name];
    if (row < 0 || row >= vec.size() || vec[row].isEmpty()) { // ⟵ evitar actualizar tombstone
        if (err) *err = tr("La fila no existe.");
        return false;
    }

    Record r = newR;
    const Schema s = m_schemas.value(name);

    const int pk = pkColumn(s);
    if (pk >= 0 && normType(s[pk].type) == "autonumeracion") {
        // No permitir cambiar el autonum
        if (r.size() <= pk || r[pk] != vec[row][pk]) {
            if (r.size() <= pk) r.resize(pk+1);
            r[pk] = vec[row][pk];
        }
    }

    // Normalización
    if (!validate(s, r, err)) return false;

    // Integridad referencial (como hija)
    if (!checkForeignKeys(name, s, r, err)) return false;

    // Unicidad (PK + únicos) respetando skipRow
    if (!checkUniqueness(name, s, r, row, err)) return false;

    // === ON UPDATE para FKs entrantes (cuando 'name' actúa como PADRE) ===
    {
        const auto incoming = incomingRelationshipsTo(name); // FKs donde 'name' es padre
        if (!incoming.isEmpty()) {
            QVector<QPair<int, QPair<QVariant,QVariant>>> parentChanges; // (parentCol, (old,new))
            for (const auto& fk : incoming) {
                const int pc = fk.parentCol;
                if (pc < 0) continue;
                if (pc >= vec[row].size()) continue;
                const QVariant oldV = vec[row][pc];
                const QVariant newV = (pc < r.size() ? r[pc] : QVariant());
                if (oldV != newV) parentChanges.push_back({pc, {oldV, newV}});
            }

            for (const auto& fk : incoming) {
                bool touches = false; QVariant oldVal, newVal;
                for (const auto& ch : parentChanges) {
                    if (ch.first == fk.parentCol) { touches = true; oldVal = ch.second.first; newVal = ch.second.second; break; }
                }
                if (!touches) continue;

                auto& childVec = m_data[fk.childTable];
                QList<int> hitRows;
                for (int i = 0; i < childVec.size(); ++i) {
                    const Record& cr = childVec[i];
                    if (cr.isEmpty()) continue; // ⟵ omitir tombstones
                    if (fk.childCol >= 0 && fk.childCol < cr.size()) {
                        if (cr[fk.childCol] == oldVal) hitRows.push_back(i);
                    }
                }
                if (hitRows.isEmpty()) continue;

                if (fk.onUpdate == FkAction::Restrict) {
                    if (err) *err = tr("Restrict: no se puede actualizar %1.%2 porque hay registros referenciando ese valor en %3.")
                                   .arg(name, m_schemas.value(name)[fk.parentCol].name, fk.childTable);
                    return false;
                } else if (fk.onUpdate == FkAction::SetNull) {
                    for (int i : hitRows) {
                        if (fk.childCol >= 0 && fk.childCol < childVec[i].size())
                            childVec[i][fk.childCol] = QVariant();
                    }
                    emit rowsChanged(fk.childTable);
                } else if (fk.onUpdate == FkAction::Cascade) {
                    for (int i : hitRows) {
                        if (fk.childCol >= 0 && fk.childCol < childVec[i].size())
                            childVec[i][fk.childCol] = newVal;
                    }
                    emit rowsChanged(fk.childTable);
                }
            }
        }
    }
    // === END ON UPDATE ===

    vec[row] = r;
    emit rowsChanged(name);
    return true;
}

bool DataModel::removeRows(const QString& name, const QList<int>& rowsToRemove, QString* err) {
    if (!m_schemas.contains(name)) return false;
    ensureTableExists(name);

    // Acciones FK entrantes respecto a 'name' como padre
    if (!handleParentDeletes(name, rowsToRemove, err)) return false;

    auto& vec  = m_data[name];
    auto& free = m_freeList[name];

    // Marcar tombstones + añadir a free list (no eliminar físicamente)
    for (int r : rowsToRemove) {
        if (r < 0 || r >= vec.size()) continue;
        if (vec[r].isEmpty()) continue;   // ya era hueco
        vec[r].clear();                   // ⟵ tombstone
        free.push_back(r);                // ⟵ agrega al Avail List (LIFO)
    }

    emit rowsChanged(name);
    return true;
}

int DataModel::columnIndex(const Schema& s, const QString& name) const {
    for (int i = 0; i < s.size(); ++i)
        if (QString::compare(s[i].name, name, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

bool DataModel::addRelationship(const QString& childTable, const QString& childColName,
                                const QString& parentTable, const QString& parentColName,
                                FkAction onDelete, FkAction onUpdate, QString* err)
{
    if (!m_schemas.contains(childTable) || !m_schemas.contains(parentTable)) {
        if (err) *err = tr("Tabla inexistente en relación.");
        return false;
    }
    const Schema cs = m_schemas.value(childTable);
    const Schema ps = m_schemas.value(parentTable);

    const int cc = columnIndex(cs, childColName);
    const int pc = columnIndex(ps, parentColName);
    if (cc < 0 || pc < 0) { if (err) *err = tr("Columna inexistente en relación."); return false; }

    // Validaciones fuertes de creación de FK
    auto normCompat = [](const FieldDef& f){
        const QString s = f.type.trimmed().toLower();
        if (s.startsWith(u"auto"))                                    return QString("autonum_long");
        if (s.startsWith(u"número") || s.startsWith(u"numero"))       return QString("numero");
        if (s.startsWith(u"fecha"))                                   return QString("fecha_hora");
        if (s.startsWith(u"moneda"))                                  return QString("moneda");
        if (s.startsWith(u"sí/no") || s.startsWith(u"si/no"))         return QString("booleano");
        if (s.startsWith(u"texto largo"))                             return QString("texto_largo");
        return QString("texto");
    };

    const FieldDef& cfd = cs[cc];
    const FieldDef& pfd = ps[pc];
    const QString ct = normCompat(cfd);
    const QString pt = normCompat(pfd);

    auto compatible = [&](){
        if (ct == pt) return true;
        // Permitir Autonumeración(Long) <-> Número
        if (ct=="autonum_long" && (pt=="numero"||pt=="autonum_long")) return true;
        if (pt=="autonum_long" && (ct=="numero"||ct=="autonum_long")) return true;
        return false;
    }();

    if (!compatible) {
        if (err) *err = tr("Tipos incompatibles: %1.%2 (%3) → %4.%5 (%6)")
                       .arg(childTable, cfd.name, cfd.type, parentTable, pfd.name, pfd.type);
        return false;
    }

    // Evitar duplicado exacto de relación
    for (const auto& existing : m_fksByChild.value(childTable)) {
        if (existing.childCol==cc && existing.parentTable==parentTable && existing.parentCol==pc) {
            if (err) *err = tr("La relación ya existe.");
            return false;
        }
    }

    // Integridad previa: no permitir crear la FK si ya hay valores huérfanos
    {
        const auto& childRows = m_data.value(childTable);
        const auto& parentRows = m_data.value(parentTable);

        auto keyOf = [](const QVariant& v)->QString {
            if (!v.isValid() || v.isNull()) return QStringLiteral("<NULL>");
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            if (v.canConvert<QDate>()) return v.toDate().toString(Qt::ISODate);
#else
            if (v.type() == QVariant::Date) return v.toDate().toString(Qt::ISODate);
#endif
            return v.toString();
        };

        QSet<QString> parentKeys;
        for (const auto& pr : parentRows) {
            if (pr.isEmpty()) continue; // ⟵ omitir tombstones
            if (pc >= 0 && pc < pr.size()) parentKeys.insert(keyOf(pr[pc]));
        }

        for (const auto& r : childRows) {
            if (r.isEmpty()) continue; // ⟵ omitir tombstones
            if (cc < 0 || cc >= r.size()) continue;
            const QVariant v = r[cc];
            if (!v.isValid() || v.isNull()) continue;
            if (!parentKeys.contains(keyOf(v))) {
                if (err) *err = tr("No se puede crear la relación: hay filas en %1.%2 sin padre en %3.%4.")
                               .arg(childTable, cfd.name, parentTable, pfd.name);
                return false;
            }
        }
    }

    ForeignKey fk;
    fk.childTable  = childTable;
    fk.childCol    = cc;
    fk.parentTable = parentTable;
    fk.parentCol   = pc;
    fk.onDelete    = onDelete;
    fk.onUpdate    = onUpdate;

    auto& vec = m_fksByChild[childTable];
    vec.push_back(fk);
    return true;
}

QVector<ForeignKey> DataModel::relationshipsFor(const QString& table) const {
    return m_fksByChild.value(table);
}

QVector<ForeignKey> DataModel::incomingRelationshipsTo(const QString& table) const {
    QVector<ForeignKey> r;
    for (auto it = m_fksByChild.constBegin(); it != m_fksByChild.constEnd(); ++it) {
        for (const auto& fk : it.value())
            if (fk.parentTable == table) r.push_back(fk);
    }
    return r;
}

// Verifica que todo valor FK (no nulo) exista en su tabla padre
bool DataModel::checkFksOnWrite(const QString& childTable, const Record& r, QString* err) const {
    const auto fks = m_fksByChild.value(childTable);
    for (const auto& fk : fks) {
        if (fk.childCol < 0 || fk.childCol >= r.size()) continue;
        const QVariant v = r[fk.childCol];
        if (!v.isValid() || v.isNull()) continue;

        const auto& parentRows = m_data.value(fk.parentTable);
        const Schema ps = m_schemas.value(fk.parentTable);
        if (fk.parentCol < 0 || fk.parentCol >= ps.size()) continue;

        bool found = false;
        for (const auto& pr : parentRows) {
            if (pr.isEmpty()) continue; // ⟵ omitir tombstones
            if (pr.size() > fk.parentCol && pr[fk.parentCol].isValid() && pr[fk.parentCol] == v) {
                found = true; break;
            }
        }
        if (!found) {
            if (err) *err = tr("Violación FK: valor \"%1\" no existe en %2.%3")
                           .arg(v.toString(), fk.parentTable, ps[fk.parentCol].name);
            return false;
        }
    }
    return true;
}

// Aplica acciones de borrado referencial para filas padre
bool DataModel::handleParentDeletes(const QString& parentTable, const QList<int>& parentRows, QString* err) {
    ensureTableExists(parentTable);

    // Obtén valores padre que van a desaparecer
    const auto& parentVec = m_data.value(parentTable);
    const Schema ps = m_schemas.value(parentTable);

    // Mapa por columna padre -> conjunto de valores
    QHash<int, QList<QVariant>> doomedValues;
    for (int r : parentRows) {
        if (r < 0 || r >= parentVec.size()) continue;
        const Record& rec = parentVec[r];
        if (rec.isEmpty()) continue; // ya es hueco
        for (const auto& fk : incomingRelationshipsTo(parentTable)) {
            if (fk.parentCol >= 0 && fk.parentCol < rec.size())
                doomedValues[fk.parentCol].append(rec[fk.parentCol]);
        }
    }

    // Recorre todas las tablas hijas afectadas
    for (auto it = m_fksByChild.begin(); it != m_fksByChild.end(); ++it) {
        const QString child = it.key();
        auto fks = it.value();
        ensureTableExists(child);
        auto& cvec = m_data[child];

        // Para cada FK que apunte a parentTable
        for (const auto& fk : fks) {
            if (fk.parentTable != parentTable) continue;

            // Filtra filas hijas que referencian a los padres a borrar
            QList<int> hitRows;
            for (int i = 0; i < cvec.size(); ++i) {
                const Record& cr = cvec[i];
                if (cr.isEmpty()) continue; // omitir huecos
                if (fk.childCol >= 0 && fk.childCol < cr.size()) {
                    const QVariant v = cr[fk.childCol];
                    if (doomedValues.value(fk.parentCol).contains(v))
                        hitRows.push_back(i);
                }
            }

            if (hitRows.isEmpty()) continue;

            if (fk.onDelete == FkAction::Restrict) {
                if (err) *err = tr("Restrict: no se puede borrar porque existen registros en %1.")
                               .arg(child);
                return false;
            } else if (fk.onDelete == FkAction::SetNull) {
                for (int i : hitRows) {
                    if (fk.childCol >= 0 && fk.childCol < cvec[i].size())
                        cvec[i][fk.childCol] = QVariant();
                }
                emit rowsChanged(child);
            } else if (fk.onDelete == FkAction::Cascade) {
                // ⟵ Usar tombstones + avail list del hijo
                auto& ffree = m_freeList[child];
                for (int r : hitRows) {
                    if (r >= 0 && r < cvec.size() && !cvec[r].isEmpty()) {
                        cvec[r].clear();
                        ffree.push_back(r);
                    }
                }
                emit rowsChanged(child);
            }
        }
    }
    return true;
}

/* ====================== Persistencia JSON ====================== */

// --- Helpers para persistencia ---
static inline QString fkActionToStr(FkAction a) {
    switch (a) {
    case FkAction::Restrict: return "Restrict";
    case FkAction::Cascade:  return "Cascade";
    case FkAction::SetNull:  return "SetNull";
    }
    return "Restrict";
}
static inline FkAction fkActionFromStr(QString s) {
    s = s.trimmed().toLower();
    if (s == "cascade")  return FkAction::Cascade;
    if (s == "setnull")  return FkAction::SetNull;
    return FkAction::Restrict;
}

// Convertir un QVariant a JSON según el tipo lógico de la columna
static QJsonValue cellToJson(const FieldDef& col, const QVariant& v) {
    if (!v.isValid() || v.isNull()) return QJsonValue(); // null
    const QString t = normType_free(col.type);
    if (t == "fecha_hora") {
        const QDate d = v.canConvert<QDate>() ? v.toDate() : QDate::fromString(v.toString(), "yyyy-MM-dd");
        return d.isValid() ? QJsonValue(d.toString("yyyy-MM-dd")) : QJsonValue();
    }
    if (t == "booleano")       return QJsonValue(v.toBool());
    if (t == "moneda")         return QJsonValue(v.toDouble());
    if (t == "numero")         return QJsonValue(static_cast<qint64>(v.toLongLong()));
    if (t == "autonumeracion") {
        if (col.autoSubtipo.trimmed().toLower().startsWith("replication"))
            return QJsonValue(v.toString());                          // GUID
        return QJsonValue(static_cast<qint64>(v.toLongLong()));       // Long Integer
    }
    // texto corto/largo
    return QJsonValue(v.toString());
}

// Inversa: JSON -> QVariant usando el esquema
static QVariant jsonToCell(const FieldDef& col, const QJsonValue& j) {
    if (j.isUndefined() || j.isNull()) return QVariant();
    const QString t = normType_free(col.type);
    if (t == "fecha_hora")    return QDate::fromString(j.toString(), "yyyy-MM-dd");
    if (t == "booleano")      return j.toBool();
    if (t == "moneda")        return j.toDouble();
    if (t == "numero")        return static_cast<qint64>(j.toDouble());
    if (t == "autonumeracion") {
        if (col.autoSubtipo.trimmed().toLower().startsWith("replication"))
            return j.toString();
        return static_cast<qint64>(j.toDouble());
    }
    return j.toString();
}

bool DataModel::saveToJson(const QString& path, QString* err) const {
    QJsonObject root;
    root["version"] = 1;

    // Tablas
    QJsonArray jt;
    for (auto it = m_schemas.constBegin(); it != m_schemas.constEnd(); ++it) {
        const QString table = it.key();
        const Schema  s     = it.value();

        QJsonObject tobj;
        tobj["name"]        = table;
        tobj["description"] = m_tableDescriptions.value(table);

        // Esquema
        QJsonArray js;
        for (const auto& c : s) {
            QJsonObject jc;
            jc["name"]               = c.name;
            jc["type"]               = c.type;
            jc["size"]               = c.size;
            jc["pk"]                 = c.pk;
            jc["formato"]            = c.formato;
            jc["autoSubtipo"]        = c.autoSubtipo;
            jc["autoNewValues"]      = c.autoNewValues;
            jc["mascaraEntrada"]     = c.mascaraEntrada;
            jc["titulo"]             = c.titulo;
            jc["reglaValidacion"]    = c.reglaValidacion;
            jc["textoValidacion"]    = c.textoValidacion;
            jc["requerido"]          = c.requerido;
            jc["indexado"]           = c.indexado;
            js.append(jc);
        }
        tobj["schema"] = js;

        // Filas: guardamos también los tombstones como arrays de nulls
        QJsonArray jrows;
        const auto& vec = m_data.value(table);
        for (const auto& r : vec) {
            QJsonArray jrow;
            if (r.isEmpty()) {
                // tombstone: todos nulls
                for (int i = 0; i < s.size(); ++i) jrow.append(QJsonValue());
            } else {
                for (int i = 0; i < s.size(); ++i) {
                    const QVariant v = (i < r.size()) ? r[i] : QVariant();
                    jrow.append(cellToJson(s[i], v));
                }
            }
            jrows.append(jrow);
        }
        tobj["rows"] = jrows;

        // Guardar también métricas de free list (opcional, solo informativo)
        tobj["freeCount"] = m_freeList.value(table).size();

        jt.append(tobj);
    }
    root["tables"] = jt;

    // Relaciones
    QJsonArray jrels;
    for (auto it = m_fksByChild.constBegin(); it != m_fksByChild.constEnd(); ++it) {
        for (const auto& fk : it.value()) {
            const Schema cs = m_schemas.value(fk.childTable);
            const Schema ps = m_schemas.value(fk.parentTable);
            QJsonObject jr;
            jr["childTable"]      = fk.childTable;
            jr["childColName"]    = (fk.childCol  >=0 && fk.childCol  < cs.size()) ? cs[fk.childCol].name   : "";
            jr["parentTable"]     = fk.parentTable;
            jr["parentColName"]   = (fk.parentCol >=0 && fk.parentCol < ps.size()) ? ps[fk.parentCol].name  : "";
            jr["onDelete"]        = fkActionToStr(fk.onDelete);
            jr["onUpdate"]        = fkActionToStr(fk.onUpdate);
            jrels.append(jr);
        }
    }
    root["relationships"] = jrels;

    // Consultas guardadas
    {
        QJsonArray jq;
        for (const auto& q : m_queries) {
            QJsonObject o; o["name"] = q.name; o["sql"] = q.sql;
            jq.append(o);
        }
        root["queries"] = jq;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) *err = tr("No se puede escribir %1").arg(path);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool DataModel::loadFromJson(const QString& path, QString* err) {
    QFile f(path);
    if (!f.exists()) return true; // nada que cargar
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = tr("No se puede abrir %1").arg(path);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (err) *err = tr("JSON inválido: %1").arg(pe.errorString());
        return false;
    }
    const QJsonObject root = doc.object();

    // Limpia y reconstruye
    m_schemas.clear();
    m_data.clear();
    m_tableDescriptions.clear();
    m_fksByChild.clear();
    m_freeList.clear();
    m_queries.clear();

    // Tablas
    for (const auto& vtab : root.value("tables").toArray()) {
        const QJsonObject tobj = vtab.toObject();
        const QString name = tobj.value("name").toString().trimmed();
        if (name.isEmpty()) continue;

        // esquema
        Schema s;
        for (const auto& vcol : tobj.value("schema").toArray()) {
            const QJsonObject jc = vcol.toObject();
            FieldDef c;
            c.name                = jc.value("name").toString();
            c.type                = jc.value("type").toString();
            c.size                = jc.value("size").toInt();
            c.pk                  = jc.value("pk").toBool();
            c.formato             = jc.value("formato").toString();
            c.autoSubtipo         = jc.value("autoSubtipo").toString();
            c.autoNewValues       = jc.value("autoNewValues").toString();
            c.mascaraEntrada      = jc.value("mascaraEntrada").toString();
            c.titulo              = jc.value("titulo").toString();
            c.reglaValidacion     = jc.value("reglaValidacion").toString();
            c.textoValidacion     = jc.value("textoValidacion").toString();
            c.requerido           = jc.value("requerido").toBool();
            c.indexado            = jc.value("indexado").toString();
            s.append(c);
        }

        m_schemas.insert(name, s);
        m_data.insert(name, {});
        m_tableDescriptions.insert(name, tobj.value("description").toString());
        m_freeList.insert(name, {});

        // filas
        const QJsonArray jrows = tobj.value("rows").toArray();
        auto& vec = m_data[name];
        auto& free = m_freeList[name];
        for (int ri = 0; ri < jrows.size(); ++ri) {
            const QJsonArray ja = jrows.at(ri).toArray();

            // Detectar si la fila es "todo nulls" => tombstone
            bool allNull = true;
            for (int i = 0; i < s.size(); ++i) {
                if (!(i < ja.size()) || !ja.at(i).isNull()) { allNull = false; break; }
            }
            if (allNull) {
                vec.push_back(Record{}); // ⟵ tombstone
                free.push_back(ri);
                continue;
            }

            Record r(s.size());
            for (int i = 0; i < s.size(); ++i) {
                const QJsonValue jv = (i < ja.size() ? ja.at(i) : QJsonValue());
                r[i] = jsonToCell(s[i], jv);
            }
            QString verr;
            validate(s, r, &verr);  // normaliza; si algo no convierte, queda null
            vec.push_back(r);
        }
    }

    // Relaciones (usa la API pública para respetar validaciones)
    for (const auto& vr : root.value("relationships").toArray()) {
        const QJsonObject jr = vr.toObject();
        const QString ctab = jr.value("childTable").toString();
        const QString ptab = jr.value("parentTable").toString();
        const QString ccol = jr.value("childColName").toString();
        const QString pcol = jr.value("parentColName").toString();
        const FkAction del = fkActionFromStr(jr.value("onDelete").toString());
        const FkAction upd = fkActionFromStr(jr.value("onUpdate").toString());
        QString dummy;
        addRelationship(ctab, ccol, ptab, pcol, del, upd, &dummy); // reconstituye
    }

    // Consultas guardadas
    for (const auto& vq : root.value("queries").toArray()) {
        const QJsonObject qo = vq.toObject();
        SavedQuery q{ qo.value("name").toString(), qo.value("sql").toString() };
        if (!q.name.trimmed().isEmpty())
            m_queries.push_back(q);
    }

    m_lastIssuedId.clear();

    return true;
}

/* ====================== Avail List: Compact / Stats ====================== */

int DataModel::compactTable(const QString& table, QString* err) {
    Q_UNUSED(err);
    if (!m_schemas.contains(table)) return 0;
    ensureTableExists(table);

    auto& vec = m_data[table];
    QVector<Record> compacted;
    compacted.reserve(vec.size());
    for (const auto& rec : vec) {
        if (!rec.isEmpty()) compacted.push_back(rec);
    }
    int removed = vec.size() - compacted.size();
    vec.swap(compacted);
    m_freeList[table].clear();
    emit rowsChanged(table);
    return removed;
}

DataModel::AvailStats DataModel::availStats(const QString& table) const {
    AvailStats st;
    auto it = m_data.constFind(table);
    if (it == m_data.constEnd()) return st;

    st.total = it->size();
    for (const auto& rec : *it) {
        if (rec.isEmpty()) ++st.deleted;
    }
    auto itf = m_freeList.constFind(table);
    st.freeSlots = (itf == m_freeList.constEnd()) ? 0 : itf->size();
    return st;
}

/* ====================== Consultas guardadas (API) ====================== */

QStringList DataModel::queries() const {
    QStringList names;
    names.reserve(m_queries.size());
    for (const auto& q : m_queries) names << q.name;
    names.sort(Qt::CaseInsensitive);
    return names;
}

QString DataModel::querySql(const QString& name) const {
    for (const auto& q : m_queries) if (q.name.compare(name, Qt::CaseInsensitive) == 0) return q.sql;
    return QString();
}

bool DataModel::saveQuery(const QString& name, const QString& sql, QString* err) {
    // si existe => update, si no => add
    for (auto& q : m_queries) {
        if (q.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
            q.sql = sql;
            emit queriesChanged();
            return true;
        }
    }
    return addQuery(name, sql, err);
}

bool DataModel::addQuery(const QString& name, const QString& sql, QString* err) {
    const QString n = name.trimmed();
    if (n.isEmpty()) { if (err) *err = tr("El nombre de la consulta no puede estar vacío."); return false; }
    for (const auto& q : m_queries) {
        if (q.name.compare(n, Qt::CaseInsensitive) == 0) {
            if (err) *err = tr("Ya existe una consulta llamada \"%1\".").arg(n);
            return false;
        }
    }
    m_queries.push_back({n, sql});
    emit queriesChanged();
    return true;
}

bool DataModel::updateQuery(const QString& name, const QString& sql, QString* err) {
    const QString n = name.trimmed();
    for (auto& q : m_queries) {
        if (q.name.compare(n, Qt::CaseInsensitive) == 0) {
            q.sql = sql;
            emit queriesChanged();
            return true;
        }
    }
    if (err) *err = tr("No existe la consulta \"%1\".").arg(n);
    return false;
}

bool DataModel::removeQuery(const QString& name, QString* err) {
    const QString n = name.trimmed();
    for (int i = 0; i < m_queries.size(); ++i) {
        if (m_queries[i].name.compare(n, Qt::CaseInsensitive) == 0) {
            m_queries.removeAt(i);
            emit queriesChanged();
            return true;
        }
    }
    if (err) *err = tr("No existe la consulta \"%1\".").arg(n);
    return false;
}
