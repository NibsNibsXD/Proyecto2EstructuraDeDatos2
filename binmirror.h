#pragma once
#include <QObject>
#include <QHash>
#include "binstorage.h"
#include "datamodel.h"

class BinMirror : public QObject {
    Q_OBJECT
public:
    static BinMirror& instance();

    void attachTo(DataModel& dm);
    void setRootPath(const QString& root);
    void setFitStrategy(FreeSpaceManager::Strategy s);

private:
    explicit BinMirror(QObject* parent=nullptr);
    BinStorage storage_;
    QHash<QString, QHash<QString, qint64>> pk2off_;

private slots:
    void onTableCreated(const QString& table);
    void onTableDropped(const QString& table);
    void onSchemaChanged(const QString& table, const Schema& s);

    void onRecordInserted(const QString& table, const QVector<QVariant>& row, const QVector<bool>& isNull, const QVariant& pkValue);
    void onRecordUpdated (const QString& table, const QVariant& pkValue, const QVector<QVariant>& newRow, const QVector<bool>& isNull);
    void onRecordDeleted (const QString& table, const QVariant& pkValue);

    void onRelationshipAdded(const QString& childTable, const QString& childField,
                             const QString& parentTable, const QString& parentField);
public:
    bool compactTable(const QString& table, QHash<qint64,qint64>& remap);
};
