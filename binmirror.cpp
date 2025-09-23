#include "binmirror.h"
#include <QDir>

BinMirror& BinMirror::instance(){ static BinMirror i; return i; }
BinMirror::BinMirror(QObject* p):QObject(p), storage_("data_bin"){}

void BinMirror::setRootPath(const QString& root){ storage_.setRoot(root); }
void BinMirror::setFitStrategy(FreeSpaceManager::Strategy s){ storage_.setFitStrategy(s); }

void BinMirror::attachTo(DataModel& dm){
    connect(&dm, &DataModel::tableCreated,  this, &BinMirror::onTableCreated);
    connect(&dm, &DataModel::tableDropped,  this, &BinMirror::onTableDropped);
    connect(&dm, &DataModel::schemaChanged, this, &BinMirror::onSchemaChanged);
    // Conecta señales CRUD si ya existen en tu DataModel; si no, llama manualmente estos slots desde la UI.
}

void BinMirror::onTableCreated(const QString& t){
    storage_.ensureTableLayout(t);
    storage_.writeSchema(t, DataModel::instance().schema(t));
}
void BinMirror::onTableDropped(const QString& t){
    storage_.dropTable(t);
    pk2off_.remove(t);
}
void BinMirror::onSchemaChanged(const QString& t, const Schema& s){
    storage_.ensureTableLayout(t);
    storage_.writeSchema(t, s);
}

void BinMirror::onRecordInserted(const QString& t, const QVector<QVariant>& row,
                                 const QVector<bool>& isNull, const QVariant& pkVal)
{
    Schema s = DataModel::instance().schema(t);
    const QByteArray packed = BinSerializer::packRow(s, row, isNull);
    const qint64 off = storage_.insertRecord(t, packed);
    pk2off_[t].insert(pkVal.toString(), off);
}

void BinMirror::onRecordUpdated(const QString& t, const QVariant& pkVal,
                                const QVector<QVariant>& newRow, const QVector<bool>& isNull)
{
    const QString key = pkVal.toString();
    qint64 off = pk2off_[t].value(key, -1);
    Schema s = DataModel::instance().schema(t);
    const QByteArray packed = BinSerializer::packRow(s, newRow, isNull);
    qint64 newOff=-1;
    if (off >= 0){
        if (storage_.updateRecord(t, off, packed, &newOff)){
            pk2off_[t][key] = newOff;
        }
    } else {
        const qint64 ins = storage_.insertRecord(t, packed);
        pk2off_[t].insert(key, ins);
    }
}

void BinMirror::onRecordDeleted(const QString& t, const QVariant& pkVal){
    const QString key = pkVal.toString();
    qint64 off = pk2off_[t].value(key, -1);
    if (off >= 0) {
        storage_.deleteRecord(t, off);
        pk2off_[t].remove(key);
    }
}

void BinMirror::onRelationshipAdded(const QString& childT, const QString& childF,
                                    const QString& parentT, const QString& parentF)
{
    const QString relDir = storage_.relDir(childT);
    QDir().mkpath(relDir);
    const QString file = relDir + QString("/%1__%2.rel").arg(childF, parentT + "." + parentF);
    QByteArray meta = BinSerializer::packSchema(childT, Schema{}, 1);
    meta.append(QString("%1|%2.%3").arg(childF, parentT, parentF).toUtf8());
    QFile f(file); if (f.open(QIODevice::WriteOnly)) { f.write(meta); f.close(); }
}

bool BinMirror::compactTable(const QString& t, QHash<qint64,qint64>& remap){
    const bool ok = storage_.compactTable(t, remap);
    if (!ok) return false;
    auto &tab = pk2off_[t];
    for (auto it = tab.begin(); it!=tab.end(); ++it){
        const qint64 oldOff = it.value();
        if (remap.contains(oldOff)) it.value() = remap.value(oldOff);
    }
    return true;
}
