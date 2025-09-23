#pragma once
#include <QString>
#include <QFile>
#include <QDir>
#include <QVector>
#include <QPair>
#include <QMutex>
#include <QMutexLocker>
#include "datamodel.h"
#include "binserializer.h"
#include "freespacemanager.h"

class BinStorage {
public:
    explicit BinStorage(QString root = QStringLiteral("data_bin"));
    BinStorage(const BinStorage&) = delete;
    BinStorage& operator=(const BinStorage&) = delete;

    void setRoot(const QString& root);

    QString tableDir(const QString& table) const;
    QString schemaPath(const QString& table) const;
    QString dataPath(const QString& table) const;
    QString indexDir(const QString& table) const;
    QString relDir(const QString& table) const;

    bool ensureTableLayout(const QString& table);
    bool dropTable(const QString& table);

    bool writeSchema(const QString& table, const Schema& s);
    bool readSchema(const QString& table, Schema& out);

    qint64 insertRecord(const QString& table, const QByteArray& packedRow);
    bool deleteRecord(const QString& table, qint64 offset);
    bool updateRecord(const QString& table, qint64 offset, const QByteArray& packedNew, qint64* newOffsetOut=nullptr);
    bool readRecord(const QString& table, qint64 offset, QByteArray& out);

    bool compactTable(const QString& table, QHash<qint64,qint64>& remap);

    void setFitStrategy(FreeSpaceManager::Strategy s);
    FreeSpaceManager::Strategy fitStrategy() const;

private:
    QString root_;
    mutable QMutex ioMutex_;
    FreeSpaceManager::Strategy strategy_ = FreeSpaceManager::Strategy::FirstFit;

    bool writeAll(const QString& path, const QByteArray& data);
    bool readAll(const QString& path, QByteArray& data) const;

    // helpers FSM usando .free de FreeSpaceManager
    bool loadFSM(const QString& table, FreeSpaceManager& fsm) const;
    bool saveFSM(const QString& table, const FreeSpaceManager& fsm) const;
};
