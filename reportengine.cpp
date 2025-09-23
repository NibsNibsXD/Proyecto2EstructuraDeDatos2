#include "reportengine.h"
#include "datamodel.h" // se usa con cuidado

#include <QRegularExpression>
#include <QMetaType>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QHash>
#include <algorithm>

// -------------------- Aliases para tipos de fila/dataset --------------------
using Row    = QMap<QString, QVariant>;
using RowVec = QVector<Row>;

Q_DECLARE_METATYPE(Row)
Q_DECLARE_METATYPE(RowVec)

// Registro estático de metatipos (necesario para QMetaObject::invokeMethod)
static int _q_register_reportengine_metatypes_ = [](){
    qRegisterMetaType<Row>("Row");
    qRegisterMetaType<RowVec>("RowVec");
    return 0;
}();

// ---------------------------------------------------------------------------

ReportEngine::ReportEngine(QObject* parent) : QObject(parent) {}

static QString norm(const QString& s){ return s.trimmed(); }

// Convierte DataModel::rows(table) -> QVector<map col→valor>
static RowVec rowsFromTable(const QString& table) {
    RowVec out;
    auto& dm = DataModel::instance();
    const auto s    = dm.schema(table);
    const auto recs = dm.rows(table);
    for (const auto& r : recs) {
        Row m;
        for (int i=0;i<s.size() && i<r.size();++i) {
            m.insert(s[i].name, r[i]);
        }
        out.push_back(m);
    }
    return out;
}

// Heurística: si tienes una consulta guardada, intenta recuperarla
static bool queryToRows(const QString& queryName, RowVec& out) {
    // Intentos no intrusivos:
    // 1) DataModel::instance().queryRows(name)    (si existiera)
    // 2) DataModel::instance().querySql(name) y luego DataModel::instance().executeSql(sql)
    // 3) fallback: si el nombre coincide con una tabla, usarla
    auto& dm = DataModel::instance();
    bool ok = false;

    // (1) método opcional
    if (dm.metaObject()->indexOfMethod("queryRows(QString)") >= 0) {
        QVector<QVector<QVariant>> raw; // placeholder
        ok = QMetaObject::invokeMethod(&dm, "queryRows", Qt::DirectConnection,
                                       Q_RETURN_ARG(QVector<QVector<QVariant>>, raw),
                                       Q_ARG(QString, queryName));
        if (ok && !raw.isEmpty()) {
            // sin esquema, no podemos etiquetar columnas; saltamos a (2)
            ok = false;
        }
    }

    // (2) obtener SQL y ejecutar
    if (!ok && dm.metaObject()->indexOfMethod("querySql(QString)") >= 0 &&
        dm.metaObject()->indexOfMethod("executeSql(QString)") >= 0)
    {
        QString sql;
        ok = QMetaObject::invokeMethod(&dm, "querySql", Qt::DirectConnection,
                                       Q_RETURN_ARG(QString, sql),
                                       Q_ARG(QString, queryName));
        if (ok && !sql.trimmed().isEmpty()) {
            RowVec res;
            bool ok2 = QMetaObject::invokeMethod(&dm, "executeSql", Qt::DirectConnection,
                                                 Q_RETURN_ARG(RowVec, res),
                                                 Q_ARG(QString, sql));
            if (ok2) { out = res; return true; }
        }
    }

    // (3) fallback tabla
    if (!ok && dm.tables().contains(queryName)) {
        out = rowsFromTable(queryName);
        return true;
    }

    return false;
}

bool ReportEngine::loadSource(const ReportSource& s, QVector<QMap<QString,QVariant>>& outRows, QString* err) {
    if (s.type == ReportSourceType::Table) {
        if (!DataModel::instance().tables().contains(s.nameOrSql)) {
            if (err) *err = QString("Tabla '%1' no existe.").arg(s.nameOrSql);
            return false;
        }
        outRows = rowsFromTable(s.nameOrSql);
        return true;

    } else if (s.type == ReportSourceType::Query) {
        RowVec tmp;
        if (!queryToRows(s.nameOrSql, tmp)) {
            if (err) *err = QString("Consulta '%1' no se pudo resolver.").arg(s.nameOrSql);
            return false;
        }
        outRows = tmp;
        return true;

    } else { // SQL crudo
        auto& dm = DataModel::instance();
        if (dm.metaObject()->indexOfMethod("executeSql(QString)") < 0) {
            if (err) *err = "El DataModel no expone executeSql(sql).";
            return false;
        }
        RowVec tmp;
        bool ok = QMetaObject::invokeMethod(&dm, "executeSql", Qt::DirectConnection,
                                            Q_RETURN_ARG(RowVec, tmp),
                                            Q_ARG(QString, s.nameOrSql));
        if (!ok) {
            if (err) *err = "Falló executeSql(sql).";
            return false;
        }
        outRows = tmp;
        return true;
    }
}

// Comparador robusto para QVariant (Qt 6 friendly)
static inline bool variantLess(const QVariant& a, const QVariant& b)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // intenta comparar numérico
    bool aNum = false, bNum = false;
    const double ad = a.toDouble(&aNum);
    const double bd = b.toDouble(&bNum);
    if (aNum && bNum) return ad < bd;

    // fecha/hora
    if (a.canConvert<QDateTime>() && b.canConvert<QDateTime>()) {
        return a.toDateTime() < b.toDateTime();
    }
    if (a.canConvert<QDate>() && b.canConvert<QDate>()) {
        return a.toDate() < b.toDate();
    }
    if (a.canConvert<QTime>() && b.canConvert<QTime>()) {
        return a.toTime() < b.toTime();
    }

    // booleanos
    if (a.metaType().id() == QMetaType::Bool || b.metaType().id() == QMetaType::Bool) {
        return a.toBool() < b.toBool();
    }

    // fallback: texto con comparación local-aware
    return QString::localeAwareCompare(a.toString(), b.toString()) < 0;
#else
    return a < b; // Qt 5 aún lo tiene
#endif
}

// --------- Evaluador simple de filtros ----------
static bool evalSimpleExpr(const QMap<QString,QVariant>& row, const QString& expr) {
    // Soporta:
    //  - "Campo = 10", "Campo != 5", "Campo > 3", "Campo >= 2", "Campo < 7", "Campo <= 1"
    //  - "Campo LIKE 'Ana%'" (comodín %)
    //  - "Campo BETWEEN '2023-01-01' AND '2023-12-31'"
    //  - Comillas simples opcionales; números detectados
    //  - Campos con espacios: usar tal cual (se busca la clave exacta)
    const QString e = expr.trimmed();
    if (e.isEmpty()) return true;

    // BETWEEN
    QRegularExpression betweenRe(R"(^\s*(.+)\s+BETWEEN\s+(.+)\s+AND\s+(.+)\s*$)", QRegularExpression::CaseInsensitiveOption);
    auto mb = betweenRe.match(e);
    if (mb.hasMatch()) {
        const QString col = norm(mb.captured(1));
        const QString v1s = norm(mb.captured(2)).remove('\'').remove('"');
        const QString v2s = norm(mb.captured(3)).remove('\'').remove('"');
        const QString val = row.value(col).toString();
        return (val >= v1s && val <= v2s);
    }

    // LIKE
    QRegularExpression likeRe(R"(^\s*(.+)\s+LIKE\s+(.+)\s*$)", QRegularExpression::CaseInsensitiveOption);
    auto ml = likeRe.match(e);
    if (ml.hasMatch()) {
        const QString col = norm(ml.captured(1));
        QString pat = norm(ml.captured(2)).remove('\'').remove('"');
        // % -> .*
        pat = QRegularExpression::escape(pat);
        pat.replace("%", ".*");
        QRegularExpression rx("^" + pat + "$", QRegularExpression::CaseInsensitiveOption);
        return rx.match(row.value(col).toString()).hasMatch();
    }

    // Comparadores simples
    QRegularExpression cmpRe(R"(^\s*(.+?)\s*(=|!=|>=|<=|>|<)\s*(.+?)\s*$)");
    auto mc = cmpRe.match(e);
    if (mc.hasMatch()) {
        const QString col = norm(mc.captured(1));
        const QString op  = mc.captured(2);
        QString rhs = norm(mc.captured(3)).remove('\'').remove('"');

        const QVariant lv = row.value(col);
        bool okNum = false;
        const double lnum = lv.toDouble(&okNum);
        bool rhsNumOk = false;
        double rnum = rhs.toDouble(&rhsNumOk);

        if (okNum && rhsNumOk) {
            if (op=="=")  return lnum == rnum;
            if (op=="!=") return lnum != rnum;
            if (op==">")  return lnum >  rnum;
            if (op==">=") return lnum >= rnum;
            if (op=="<")  return lnum <  rnum;
            if (op=="<=") return lnum <= rnum;
        } else {
            const QString ls = lv.toString();
            if (op=="=")  return ls == rhs;
            if (op=="!=") return ls != rhs;
            if (op==">")  return ls >  rhs;
            if (op==">=") return ls >= rhs;
            if (op=="<")  return ls <  rhs;
            if (op=="<=") return ls <= rhs;
        }
    }

    // Si no se pudo parsear, no filtrar
    return true;
}

// -------------------- Operaciones de dataset --------------------

bool ReportEngine::applyJoin(const JoinDef& j,
                             const QVector<QMap<QString,QVariant>>& left,
                             const QVector<QMap<QString,QVariant>>& right,
                             QVector<QMap<QString,QVariant>>& out,
                             QString* /*err*/)
{
    out.clear();

    // Qt 6: QVariant ya no tiene operator< → usamos hash por texto
    QMultiHash<QString, QMap<QString,QVariant>> idx;
    idx.reserve(right.size());
    for (const auto& row : right) {
        const QString key = row.value(j.rightField).toString();
        idx.insert(key, row);
    }

    // LEFT o INNER
    for (const auto& l : left) {
        const QString k = l.value(j.leftField).toString();
        const auto matches = idx.values(k);

        if (matches.isEmpty()) {
            if (j.leftJoin) {
                QMap<QString,QVariant> m = l;
                // marcador mínimo para señalar ausencia (no tenemos esquema aquí)
                m.insert(QString("%1<no_match>").arg(j.prefixRight), QVariant());
                out.push_back(m);
            }
            continue;
        }
        for (const auto& r : matches) {
            QMap<QString,QVariant> m = l;
            // fusionar columnas de la derecha con prefijo
            for (auto it = r.constBegin(); it != r.constEnd(); ++it) {
                m.insert(j.prefixRight + it.key(), it.value());
            }
            out.push_back(m);
        }
    }
    return true;
}

void ReportEngine::applyFilters(const QVector<FilterDef>& filters, QVector<QMap<QString,QVariant>>& io) {
    if (filters.isEmpty()) return;
    QVector<QMap<QString,QVariant>> out;
    out.reserve(io.size());
    for (const auto& r : io) {
        bool keep = true;
        for (const auto& f : filters) {
            if (!evalSimpleExpr(r, f.expr)) { keep = false; break; }
        }
        if (keep) out.push_back(r);
    }
    io.swap(out);
}

void ReportEngine::applySorts(const QVector<SortDef>& sorts, QVector<QMap<QString,QVariant>>& io) {
    if (sorts.isEmpty()) return;
    std::stable_sort(io.begin(), io.end(), [&](const Row& a, const Row& b){
        for (const auto& s : sorts) {
            const QVariant va = a.value(s.field);
            const QVariant vb = b.value(s.field);
            if (va == vb) continue;
            const bool less = variantLess(va, vb);
            return (s.order == Qt::AscendingOrder) ? less : !less;
        }
        return false;
    });
}

QStringList ReportEngine::inferHeaders(const ReportDef& def, const RowVec& raw) const {
    // Si el usuario definió fields → respetar orden/alias
    if (!def.fields.isEmpty()) {
        QStringList out;
        for (const auto& f : def.fields)
            out << (f.alias.isEmpty()? f.field : f.alias);
        return out;
    }
    // Si no, tomar todas las claves de la primera fila
    if (!raw.isEmpty()) return raw.first().keys();
    return {};
}

bool ReportEngine::build(const ReportDef& def, ReportDataset* out, QString* err) {
    if (!out) { if(err)*err="Parámetro de salida nulo."; return false; }

    // 1) cargar origen principal
    RowVec cur;
    {
        QVector<QMap<QString,QVariant>> tmp;
        if (!loadSource(def.mainSource, tmp, err)) {
            // no crashear: dataset vacío
            out->clear();
            return false;
        }
        cur = tmp;
    }

    // 2) mapa de alias→dataset para joins
    QMap<QString, RowVec> sources;
    sources.insert("main", cur);

    // cargar extras
    for (int i=0;i<def.extraSources.size();++i) {
        QVector<QMap<QString,QVariant>> tmp;
        QString e2;
        if (loadSource(def.extraSources[i], tmp, &e2)) {
            const QString key = QString("s%1").arg(i+1);
            sources.insert(key, tmp);
        }
        // si falla, ignoramos la fuente sin romper
    }

    // 3) aplicar joins en cadena (sobre "cur")
    for (const auto& j : def.joins) {
        const auto left  = sources.value(j.leftSource, cur);
        const auto right = sources.value(j.rightSource);
        if (right.isEmpty()) {
            // si no existe la fuente derecha, saltar sin romper
            continue;
        }
        RowVec joined;
        QString e3;
        if (!applyJoin(j, left, right, joined, &e3)) {
            // si falla, continuar con 'left'
            continue;
        }
        cur = joined;
        sources["main"] = cur; // actualizar “main” si los siguientes joins usan main
    }

    // 4) filtros
    {
        RowVec tmp = cur;
        applyFilters(def.filters, tmp);
        cur = tmp;
    }

    // 5) ordenamientos
    QVector<SortDef> sorts = def.sorts;
    // si hay grupos y no hay sorts, ordenar por grupos primero para que agregados por grupo funcionen mejor
    if (sorts.isEmpty() && !def.groups.isEmpty()) {
        for (const auto& g : def.groups)
            sorts.push_back({g.field, g.order});
    }
    {
        RowVec tmp = cur;
        applySorts(sorts, tmp);
        cur = tmp;
    }

    // 6) convertir a ReportDataset respetando fields
    out->clear();
    out->headers = inferHeaders(def, cur);

    const bool hasExplicit = !def.fields.isEmpty();
    for (const auto& m : cur) {
        ReportRow rr;
        if (hasExplicit) {
            for (const auto& f : def.fields) {
                const QString key = f.field; // columna real en mapa
                rr.cols.insert(f.alias.isEmpty()? key : f.alias, m.value(key));
            }
        } else {
            rr.cols = m; // todas las columnas
        }
        out->rows.push_back(rr);
    }

    return true;
}
