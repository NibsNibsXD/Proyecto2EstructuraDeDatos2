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
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <QJsonArray>

/* ====================== Singleton ====================== */

DataModel& DataModel::instance() {
    static DataModel inst;
    return inst;
}

DataModel::DataModel(QObject* parent) : QObject(parent) {}

/* ====================== Helpers ====================== */

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
static inline QString normType(const QString& t) {
    const QString s = t.trimmed().toLower();
    if (s.startsWith(u"auto"))                                    return "autonumeracion";
    if (s.startsWith(u"número") || s.startsWith(u"numero"))       return "numero";
    if (s.startsWith(u"fecha"))                                   return "fecha_hora";
    if (s.startsWith(u"moneda"))                                  return "moneda";
    if (s.startsWith(u"sí/no") || s.startsWith(u"si/no"))          return "booleano";
    if (s.startsWith(u"texto largo"))                              return "texto_largo";
    return "texto"; // por defecto (Texto corto)
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

QVariant DataModel::nextAutoNumber(const QString& name) const {
    const Schema sch = m_schemas.value(name);
    const int pk = pkColumn(sch);
    if (pk < 0) return 1;

    const FieldDef& fd = sch[pk];
    auto it = m_data.constFind(name);

    // Si el subtipo es Replication ID -> devolver GUID (string)
    if (normType(fd.type) == "autonumeracion" &&
        fd.autoSubtipo.trimmed().toLower().startsWith("replication"))
    {
        return QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    // Sin filas aún
    if (it == m_data.constEnd() || it->isEmpty()) {
        if (fd.autoNewValues.toLower().startsWith("random")) {
            return static_cast<qint64>(QRandomGenerator::global()->bounded(1, INT_MAX));
        }
        return static_cast<qint64>(1);
    }

    // Random entero único
    if (fd.autoNewValues.toLower().startsWith("random")) {
        while (true) {
            qint64 v = static_cast<qint64>(QRandomGenerator::global()->bounded(1, INT_MAX));
            bool clash = false;
            for (const auto& rec : it.value()) {
                if (rec.value(pk).toLongLong() == v) { clash = true; break; }
            }
            if (!clash) return v;
        }
    }

    // Incremental: max + 1
    qlonglong mx = 0;
    for (const auto& r : it.value()) {
        bool ok = false;
        qlonglong v = r.value(pk).toLongLong(&ok);
        if (ok) mx = std::max(mx, v);
    }
    return mx + 1;
}


bool DataModel::normalizeValue(const FieldDef& col, QVariant& v, QString* err) const {
    if (isEmptyVar(v)) {
        // si es requerido (y no PK), no puede estar vacío
        if (col.requerido && !col.pk) {
            if (err) *err = tr("El campo \"%1\" es requerido.").arg(col.name);
            return false;
        }
        v = QVariant(); // null aceptado
        return true;
    }

    const QString t = normType(col.type);

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
            if (!okD) { if (err) *err = tr("El campo \"%1\" debe ser entero.").arg(col.name); return false; }
            // Rechaza fracciones: 12.5 no vale para entero
            if (std::fabs(d - std::llround(d)) > 1e-9) {
                if (err) *err = tr("El campo \"%1\" debe ser entero.").arg(col.name);
                return false;
            }
            v = static_cast<qint64>(std::llround(d));
            return true;
        } else {
            bool ok = false;
            const double d = v.toDouble(&ok);
            if (!ok) { if (err) *err = tr("El campo \"%1\" debe ser numérico.").arg(col.name); return false; }
            v = d;                    // guarda como double
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
        if (v.canConvert<QDate>()) {
            QDate d = v.toDate();
            if (!d.isValid()) { if (err) *err = tr("Fecha inválida en \"%1\".").arg(col.name); return false; }
            v = d;
            return true;
        }
        const QString s = v.toString().trimmed();
        static const char* fmts[] = {"yyyy-MM-dd","dd/MM/yyyy","dd-MM-yyyy","MM/dd/yyyy"};
        QDate d;
        for (auto f : fmts) { d = QDate::fromString(s, f); if (d.isValid()) break; }
        if (!d.isValid()) { if (err) *err = tr("Fecha inválida en \"%1\". Use yyyy-MM-dd.").arg(col.name); return false; }
        v = d;
        return true;
    }

    if (t == "booleano") {
        // Acepta true/false, 1/0, sí/no, si/no, yes/no
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
        const QVariant& v = vec[i].value(pkCol);
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

bool DataModel::createTable(const QString& name, const Schema& s, QString* err) {
    if (!isValidTableName(name)) { if (err) *err = tr("Nombre de tabla inválido: %1").arg(name); return false; }
    if (m_schemas.contains(name)) { if (err) *err = tr("La tabla ya existe: %1").arg(name); return false; }

    // ✅ Aceptar esquema vacío al crear; el usuario lo diseñará luego.
    // (si quieres, puedes dejar validación solo cuando s NO esté vacío)
    if (!s.isEmpty()) {
        QSet<QString> names; int pkCount = 0;
        for (const auto& c : s) {
            if (c.name.trimmed().isEmpty()) { if (err) *err = tr("Columna sin nombre."); return false; }
            if (names.contains(c.name))      { if (err) *err = tr("Columna duplicada: %1").arg(c.name); return false; }
            names.insert(c.name);
            if (c.pk) ++pkCount;
        }
        if (pkCount > 1) { if (err) *err = tr("Solo se permite una PK."); return false; }
    }

    m_schemas.insert(name, s);
    m_data.insert(name, {});
    m_tableDescriptions.insert(name, QString());
    emit tableCreated(name);
    emit schemaChanged(name, s);
    return true;
}



bool DataModel::dropTable(const QString& name, QString* err) {
    if (!m_schemas.contains(name)) { if (err) *err = tr("No existe la tabla: %1").arg(name); return false; }
    m_schemas.remove(name);
    m_data.remove(name);
    m_tableDescriptions.remove(name);
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
    emit tableDropped(oldName);
    emit tableCreated(newName);
    emit schemaChanged(newName, m_schemas.value(newName));
    return true;
}

bool DataModel::setSchema(const QString& name, const Schema& s, QString* err) {
    if (!m_schemas.contains(name)) { if (err) *err = tr("No existe la tabla: %1").arg(name); return false; }

    // Re-validación básica
    QSet<QString> names;
    int pkCount = 0;
    for (const auto& c : s) {
        if (c.name.trimmed().isEmpty()) { if (err) *err = tr("Columna sin nombre."); return false; }
        if (names.contains(c.name))      { if (err) *err = tr("Columna duplicada: %1").arg(c.name); return false; }
        names.insert(c.name);
        if (c.pk) ++pkCount;
    }
    if (pkCount > 1) { if (err) *err = tr("Solo se permite una PK."); return false; }

    // Migración sencilla por nombre
    const Schema oldS  = m_schemas.value(name);
    const auto   oldRs = m_data.value(name);

    QHash<QString,int> oldIndex;
    for (int i = 0; i < oldS.size(); ++i) oldIndex.insert(oldS[i].name, i);

    QVector<Record> newRows;
    newRows.reserve(oldRs.size());

    for (const auto& r : oldRs) {
        Record nr(s.size());
        for (int i = 0; i < s.size(); ++i) {
            const FieldDef& col = s[i];
            const int oi = oldIndex.value(col.name, -1);
            QVariant v = (oi >= 0 && oi < r.size()) ? r[oi] : QVariant();

            QString convErr;
            if (!normalizeValue(col, v, &convErr)) {
                v = QVariant(); // si no convierte, lo dejamos nulo
            }
            nr[i] = v;
        }
        newRows.append(nr);
    }

    m_schemas[name] = s;
    m_data[name]    = newRows;
    emit schemaChanged(name, s);
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
    const Schema s = m_schemas.value(name);

    // Autonumera si aplica y si viene vacío
    const int pk = pkColumn(s);
    if (pk >= 0 && normType(s[pk].type) == "autonumeracion") {
        r.resize(std::max(r.size(), s.size()));
        if (isEmptyVar(r[pk])) {
            r[pk] = nextAutoNumber(name);
        }
    }

    if (!validate(s, r, err)) return false;
    if (!checkFksOnWrite(name, r, err)) return false;  // ⟵ NUEVO



    if (pk >= 0) {
        if (!ensureUniquePk(name, pk, r[pk], -1, err)) return false;
    }

    m_data[name].push_back(r);
    emit rowsChanged(name);
    return true;
}

bool DataModel::updateRow(const QString& name, int row, const Record& newR, QString* err) {
    if (!m_schemas.contains(name)) { if (err) *err = tr("No existe la tabla: %1").arg(name); return false; }
    auto& vec = m_data[name];
    if (row < 0 || row >= vec.size()) { if (err) *err = tr("Fila fuera de rango."); return false; }

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

    if (!validate(s, r, err)) return false;
    if (!checkFksOnWrite(name, r, err)) return false;  // ⟵ NUEVO


    if (pk >= 0) {
        if (!ensureUniquePk(name, pk, r[pk], row, err)) return false;
    }

    vec[row] = r;
    emit rowsChanged(name);
    return true;
}

bool DataModel::removeRows(const QString& name, const QList<int>& rowsToRemove, QString* err) {
    if (!m_schemas.contains(name)) return false;

    // ⟵ NUEVO: chequear/ejecutar acciones FK entrantes
    if (!handleParentDeletes(name, rowsToRemove, err)) return false;

    auto& vec = m_data[name];
    QList<int> rr = rowsToRemove;
    std::sort(rr.begin(), rr.end(), std::greater<int>());
    for (int r : rr) if (r >= 0 && r < vec.size()) vec.remove(r);
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

    // Nota: no exigimos que el padre sea PK, pero es lo normal.
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
        // Null permitido si la columna no es "requerido"
        if (!v.isValid() || v.isNull()) continue;

        const auto& parentRows = m_data.value(fk.parentTable);
        const Schema ps = m_schemas.value(fk.parentTable);
        if (fk.parentCol < 0 || fk.parentCol >= ps.size()) continue;

        bool found = false;
        for (const auto& pr : parentRows) {
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
    // Obtén valores padre que van a desaparecer
    const auto& parentVec = m_data.value(parentTable);
    const Schema ps = m_schemas.value(parentTable);

    // Mapa por columna padre -> conjunto de valores
    QHash<int, QList<QVariant>> doomedValues;
    for (int r : parentRows) {
        if (r < 0 || r >= parentVec.size()) continue;
        const Record& rec = parentVec[r];
        for (const auto& fk : incomingRelationshipsTo(parentTable)) {
            if (fk.parentCol >= 0 && fk.parentCol < rec.size())
                doomedValues[fk.parentCol].append(rec[fk.parentCol]);
        }
    }

    // Recorre todas las tablas hijas afectadas
    for (auto it = m_fksByChild.begin(); it != m_fksByChild.end(); ++it) {
        const QString child = it.key();
        auto fks = it.value();
        auto& cvec = m_data[child];
        const Schema cs = m_schemas.value(child);

        // Para cada FK que apunte a parentTable
        for (const auto& fk : fks) {
            if (fk.parentTable != parentTable) continue;

            // Filtra filas hijas que referencian a los padres a borrar
            QList<int> hitRows;
            for (int i = 0; i < cvec.size(); ++i) {
                const Record& cr = cvec[i];
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
                // Setea null en la FK
                for (int i : hitRows) {
                    if (fk.childCol >= 0 && fk.childCol < cvec[i].size())
                        cvec[i][fk.childCol] = QVariant();
                }
                emit rowsChanged(child);
            } else if (fk.onDelete == FkAction::Cascade) {
                // Borrado en cascada
                QList<int> toDel = hitRows;
                std::sort(toDel.begin(), toDel.end(), std::greater<int>());
                for (int r : toDel) if (r >= 0 && r < cvec.size()) cvec.remove(r);
                emit rowsChanged(child);
            }
        }
    }
    return true;
}


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
    const QString t = normType(col.type);
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
    const QString t = normType(col.type);
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
            jc["valorPredeterminado"]= c.valorPredeterminado;
            jc["reglaValidacion"]    = c.reglaValidacion;
            jc["textoValidacion"]    = c.textoValidacion;
            jc["requerido"]          = c.requerido;
            jc["indexado"]           = c.indexado;
            js.append(jc);
        }
        tobj["schema"] = js;

        // Filas
        QJsonArray jrows;
        const auto& vec = m_data.value(table);
        for (const auto& r : vec) {
            QJsonArray jrow;
            for (int i = 0; i < s.size(); ++i) {
                const QVariant v = (i < r.size()) ? r[i] : QVariant();
                jrow.append(cellToJson(s[i], v));
            }
            jrows.append(jrow);
        }
        tobj["rows"] = jrows;

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
            c.valorPredeterminado = jc.value("valorPredeterminado").toString();
            c.reglaValidacion     = jc.value("reglaValidacion").toString();
            c.textoValidacion     = jc.value("textoValidacion").toString();
            c.requerido           = jc.value("requerido").toBool();
            c.indexado            = jc.value("indexado").toString();
            s.append(c);
        }

        m_schemas.insert(name, s);
        m_data.insert(name, {});
        m_tableDescriptions.insert(name, tobj.value("description").toString());

        // filas
        const QJsonArray jrows = tobj.value("rows").toArray();
        auto& vec = m_data[name];
        for (const auto& vrow : jrows) {
            const QJsonArray ja = vrow.toArray();
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

    return true;
}

