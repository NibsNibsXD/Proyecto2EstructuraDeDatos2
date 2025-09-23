#ifndef REPORTENGINE_H
#define REPORTENGINE_H

#include <QObject>
#include <QVector>
#include <QVariant>
#include <QMap>
#include <QSet>
#include <QSharedPointer>
#include "reportdef.h"

// Forward declaration para evitar dependencias duras
class DataModel;

struct ReportRow {
    // mapa de columna→valor, resultado final tras joins/filtros
    QMap<QString, QVariant> cols;
};

class ReportDataset {
public:
    QVector<ReportRow> rows;
    QStringList headers; // orden de columnas finales

    void clear() { rows.clear(); headers.clear(); }
    int rowCount() const { return rows.size(); }

    QVariant value(int r, const QString& col) const {
        if (r < 0 || r >= rows.size()) return {};
        return rows[r].cols.value(col);
    }
};

class ReportEngine : public QObject {
    Q_OBJECT
public:
    explicit ReportEngine(QObject* parent=nullptr);

    // Construye el dataset en memoria aplicando:
    // 1) lectura de fuente principal y extras
    // 2) joins (comparaciones entre tablas/consultas)
    // 3) filtros
    // 4) ordenaciones
    // Devuelve false si algo crítico falla (pero sin crashear)
    bool build(const ReportDef& def, ReportDataset* out, QString* err=nullptr);

private:
    // Utils
    bool loadSource(const ReportSource& s, QVector<QMap<QString,QVariant>>& outRows, QString* err);
    bool applyJoin(const JoinDef& j,
                   const QVector<QMap<QString,QVariant>>& left,
                   const QVector<QMap<QString,QVariant>>& right,
                   QVector<QMap<QString,QVariant>>& out,
                   QString* err);
    void applyFilters(const QVector<FilterDef>& filters, QVector<QMap<QString,QVariant>>& io);
    void applySorts(const QVector<SortDef>& sorts, QVector<QMap<QString,QVariant>>& io);
    QStringList inferHeaders(const ReportDef& def, const QVector<QMap<QString,QVariant>>& raw) const;
};

#endif // REPORTENGINE_H
