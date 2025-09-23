#ifndef REPORTDEF_H
#define REPORTDEF_H

#include <QObject>
#include <QVector>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QMetaType>

enum class ReportSourceType { Table, Query, Sql };

enum class AggFn { Sum, Count, Avg, Min, Max, None };

struct ReportSource {
    ReportSourceType type = ReportSourceType::Table;
    QString nameOrSql; // table name, query name, or raw SQL
};

struct ReportField {
    QString field;     // nombre de columna en dataset final
    QString alias;     // etiqueta a mostrar
    int width = 120;   // ancho sugerido en px
    Qt::Alignment align = Qt::AlignLeft;
};

struct GroupDef {
    QString field;
    Qt::SortOrder order = Qt::AscendingOrder;
    bool pageBreakAfter = false;
};

struct SortDef {
    QString field;
    Qt::SortOrder order = Qt::AscendingOrder;
};

struct AggDef {
    QString field;
    AggFn fn = AggFn::None;    // suma/avg/etc
    QString scope;             // "group:<field>" o "report"
};

struct FilterDef {
    QString expr; // expresión simple tipo: Campo > 10, Nombre LIKE 'A%', Fecha BETWEEN '2023-01-01' AND '2023-12-31'
};

struct JoinDef {
    // Para comparaciones/joins entre tablas/consultas
    // JOIN tableB ON tableA.key == tableB.key (inner/left)
    QString leftSource;   // nombre tabla/consulta (tal como se elige en wizard); "main" para el origen principal
    QString rightSource;  // idem
    QString leftField;
    QString rightField;
    bool leftJoin = true; // true: LEFT JOIN, false: INNER JOIN
    QString prefixRight;  // prefijo para columnas de la derecha (evitar colisiones)
};

struct LayoutDef {
    QString templateName = "tabular"; // tabular | cards
    QString paper = "Letter";
    int marginL = 12, marginT = 12, marginR = 12, marginB = 12;
    int rowHeight = 22;
    bool zebra = true;
    bool showHeader = true;
    bool showFooter = true;
};

struct ReportDef {
    QString name;
    ReportSource mainSource;
    // fuentes adicionales (para joins/comparaciones)
    QVector<ReportSource> extraSources;
    QVector<JoinDef> joins;

    QVector<ReportField> fields;     // columnas a mostrar (del dataset final)
    QVector<GroupDef> groups;
    QVector<SortDef>  sorts;
    QVector<AggDef>   aggregates;
    QVector<FilterDef> filters;

    LayoutDef layout;

    // ==== Serialización a JSON (para guardar junto al resto del proyecto) ====
    QJsonObject toJson() const;
    static ReportDef fromJson(const QJsonObject& o);

    // Helpers
    QStringList fieldNames() const {
        QStringList out;
        for (const auto& f : fields) out << (f.alias.isEmpty()? f.field : f.alias);
        return out;
    }
};

inline QString aggToString(AggFn a) {
    switch (a) {
    case AggFn::Sum:   return "sum";
    case AggFn::Count: return "count";
    case AggFn::Avg:   return "avg";
    case AggFn::Min:   return "min";
    case AggFn::Max:   return "max";
    default: return "none";
    }
}
inline AggFn aggFromString(const QString& s) {
    const QString t = s.toLower().trimmed();
    if (t=="sum") return AggFn::Sum;
    if (t=="count") return AggFn::Count;
    if (t=="avg") return AggFn::Avg;
    if (t=="min") return AggFn::Min;
    if (t=="max") return AggFn::Max;
    return AggFn::None;
}

#endif // REPORTDEF_H
