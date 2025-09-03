#include "datamodel.h"

#include <QtGlobal>
#include <QRegularExpression>
#include <QDate>
#include <QSet>
#include <QHash>
#include <algorithm>

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
    return "texto"; // por defecto
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
    auto it = m_data.constFind(name);
    if (it == m_data.constEnd() || it->isEmpty()) return 1;

    const Schema sch = m_schemas.value(name);
    const int pk = pkColumn(sch);
    if (pk < 0) return 1;

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
        // aquí solo verificamos que sea entero si viene con valor
        bool ok = false;
        v.toLongLong(&ok);
        if (!ok) { if (err) *err = tr("El campo \"%1\" debe ser entero.").arg(col.name); return false; }
        v = v.toLongLong();
        return true;
    }
    if (t == "numero") {
        bool ok = false; qlonglong iv = v.toLongLong(&ok);
        if (!ok) { if (err) *err = tr("El campo \"%1\" debe ser número entero.").arg(col.name); return false; }
        v = static_cast<qint64>(iv);
        return true;
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

    // Texto corto
    QString s = v.toString();
    if (col.size > 0 && s.size() > col.size) s.truncate(col.size);
    v = s;
    return true;
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

bool DataModel::createTable(const QString& name, const Schema& s, QString* err) {
    if (!isValidTableName(name)) {
        if (err) *err = tr("Nombre de tabla inválido: %1").arg(name);
        return false;
    }
    if (m_schemas.contains(name)) {
        if (err) *err = tr("La tabla ya existe: %1").arg(name);
        return false;
    }
    if (s.isEmpty()) {
        if (err) *err = tr("El esquema no puede estar vacío.");
        return false;
    }

    // Validar duplicados y 0..1 PK
    QSet<QString> names;
    int pkCount = 0;
    for (const auto& c : s) {
        if (c.name.trimmed().isEmpty()) { if (err) *err = tr("Columna sin nombre."); return false; }
        if (names.contains(c.name))      { if (err) *err = tr("Columna duplicada: %1").arg(c.name); return false; }
        names.insert(c.name);
        if (c.pk) ++pkCount;
    }
    if (pkCount > 1) { if (err) *err = tr("Solo se permite una PK."); return false; }

    m_schemas.insert(name, s);
    m_data.insert(name, {});
    emit tableCreated(name);
    emit schemaChanged(name, s);
    return true;
}

bool DataModel::dropTable(const QString& name, QString* err) {
    if (!m_schemas.contains(name)) { if (err) *err = tr("No existe la tabla: %1").arg(name); return false; }
    m_schemas.remove(name);
    m_data.remove(name);
    emit tableDropped(name);
    return true;
}

bool DataModel::renameTable(const QString& oldName, const QString& newName, QString* err) {
    if (!m_schemas.contains(oldName)) { if (err) *err = tr("No existe: %1").arg(oldName); return false; }
    if (!isValidTableName(newName))   { if (err) *err = tr("Nombre inválido: %1").arg(newName); return false; }
    if (m_schemas.contains(newName))  { if (err) *err = tr("Ya existe: %1").arg(newName); return false; }

    m_schemas.insert(newName, m_schemas.take(oldName));
    m_data.insert(newName,   m_data.take(oldName));
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

    if (pk >= 0) {
        if (!ensureUniquePk(name, pk, r[pk], row, err)) return false;
    }

    vec[row] = r;
    emit rowsChanged(name);
    return true;
}

bool DataModel::removeRows(const QString& name, const QList<int>& rowsToRemove, QString* /*err*/) {
    if (!m_schemas.contains(name)) return false;
    auto& vec = m_data[name];
    QList<int> rr = rowsToRemove;
    std::sort(rr.begin(), rr.end(), std::greater<int>());
    for (int r : rr) if (r >= 0 && r < vec.size()) vec.remove(r);
    emit rowsChanged(name);
    return true;
}
