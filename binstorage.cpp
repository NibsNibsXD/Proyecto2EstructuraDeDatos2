#include "binstorage.h"
#include <QSaveFile>
#include <QDataStream>
#include <QFileInfo>

BinStorage::BinStorage(QString root) : root_(std::move(root)) {
    QDir().mkpath(root_);
    QDir().mkpath(root_ + "/tables");
    QDir().mkpath(root_ + "/logs");
}
void BinStorage::setRoot(const QString& r){
    QMutexLocker lk(&ioMutex_);
    root_ = r;
    QDir().mkpath(root_);
    QDir().mkpath(root_ + "/tables");
    QDir().mkpath(root_ + "/logs");
}

QString BinStorage::tableDir(const QString& t) const { return root_ + "/tables/" + t; }
QString BinStorage::schemaPath(const QString& t) const { return tableDir(t) + "/schema.meta"; }
QString BinStorage::dataPath(const QString& t) const { return tableDir(t) + "/data.mad"; }
QString BinStorage::indexDir(const QString& t) const { return tableDir(t) + "/index"; }
QString BinStorage::relDir(const QString& t) const { return tableDir(t) + "/rel"; }

bool BinStorage::ensureTableLayout(const QString& t){
    QMutexLocker lk(&ioMutex_);
    QDir().mkpath(tableDir(t));
    QDir().mkpath(indexDir(t));
    QDir().mkpath(relDir(t));
    QFile f(dataPath(t));
    if (!f.exists()){
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.close();
        // inicializa .free vacío
        FreeSpaceManager fsm; fsm.setDataPath(dataPath(t)); fsm.save(nullptr);
    }
    return true;
}
bool BinStorage::dropTable(const QString& t){
    QMutexLocker lk(&ioMutex_);
    QDir d(tableDir(t));
    if (!d.exists()) return true;
    return d.removeRecursively();
}

bool BinStorage::writeAll(const QString& p, const QByteArray& data){
    QSaveFile sf(p);
    if (!sf.open(QIODevice::WriteOnly)) return false;
    if (sf.write(data) != data.size()) return false;
    return sf.commit();
}
bool BinStorage::readAll(const QString& p, QByteArray& out) const{
    QFile f(p); if (!f.open(QIODevice::ReadOnly)) return false;
    out = f.readAll(); return true;
}

bool BinStorage::writeSchema(const QString& t, const Schema& s){
    QMutexLocker lk(&ioMutex_);
    if (!ensureTableLayout(t)) return false;
    const QByteArray meta = BinSerializer::packSchema(t, s, 1);
    return writeAll(schemaPath(t), meta);
}
bool BinStorage::readSchema(const QString& t, Schema& out){
    QMutexLocker lk(&ioMutex_);
    QByteArray meta; if (!readAll(schemaPath(t), meta)) return false;
    QString table; quint32 ver=0;
    return BinSerializer::unpackSchema(meta, table, out, &ver);
}

void BinStorage::setFitStrategy(FreeSpaceManager::Strategy s){ strategy_ = s; }
FreeSpaceManager::Strategy BinStorage::fitStrategy() const { return strategy_; }

bool BinStorage::loadFSM(const QString& t, FreeSpaceManager& fsm) const{
    fsm.setDataPath(dataPath(t));
    QString err;
    fsm.load(&err); // si no existe, queda vacío
    fsm.setStrategy(strategy_);
    return true;
}
bool BinStorage::saveFSM(const QString& /*t*/, const FreeSpaceManager& fsm) const{
    return const_cast<FreeSpaceManager&>(fsm).save(nullptr);
}

qint64 BinStorage::insertRecord(const QString& t, const QByteArray& packed){
    QMutexLocker lk(&ioMutex_);
    if (!ensureTableLayout(t)) return -1;
    QFile f(dataPath(t)); if (!f.open(QIODevice::ReadWrite)) return -1;

    FreeSpaceManager fsm; loadFSM(t, fsm);
    const qint64 need = packed.size();
    qint64 pos = -1;
    if (fsm.allocate(need, &pos)){
        f.seek(pos);
        if (f.write(packed) != need) return -1;
    } else {
        pos = f.size();
        f.seek(pos);
        if (f.write(packed) != need) return -1;
    }
    saveFSM(t, fsm);
    return pos;
}

bool BinStorage::deleteRecord(const QString& t, qint64 off){
    QMutexLocker lk(&ioMutex_);
    QFile f(dataPath(t)); if (!f.open(QIODevice::ReadWrite)) return false;
    if (off < 0 || off >= f.size()) return false;

    f.seek(off);
    quint32 payloadLen=0; if (f.read(reinterpret_cast<char*>(&payloadLen), sizeof(payloadLen)) != sizeof(payloadLen)) return false;
    quint32 recLen = payloadLen + sizeof(payloadLen);

    f.seek(off + sizeof(quint32));
    quint8 tomb=1; if (f.write(reinterpret_cast<const char*>(&tomb), 1) != 1) return false;

    FreeSpaceManager fsm; loadFSM(t, fsm);
    fsm.freeBlock(off, recLen);
    saveFSM(t, fsm);
    return true;
}

bool BinStorage::updateRecord(const QString& t, qint64 off, const QByteArray& packed, qint64* newOff){
    QMutexLocker lk(&ioMutex_);
    QFile f(dataPath(t)); if (!f.open(QIODevice::ReadWrite)) return false;
    if (off < 0 || off >= f.size()) return false;

    f.seek(off);
    quint32 payloadLen=0; if (f.read(reinterpret_cast<char*>(&payloadLen), sizeof(payloadLen)) != sizeof(payloadLen)) return false;
    quint32 oldLen = payloadLen + sizeof(payloadLen);
    const quint32 newLen = packed.size();

    if (newLen <= oldLen){
        f.seek(off);
        if (f.write(packed) != newLen) return false;
        if (oldLen > newLen){
            FreeSpaceManager fsm; loadFSM(t, fsm);
            fsm.freeBlock(off + newLen, oldLen - newLen);
            saveFSM(t, fsm);
        }
        if (newOff) *newOff = off;
        return true;
    } else {
        if (!deleteRecord(t, off)) return false;
        const qint64 p = insertRecord(t, packed);
        if (newOff) *newOff = p;
        return p >= 0;
    }
}

bool BinStorage::readRecord(const QString& t, qint64 off, QByteArray& out){
    QMutexLocker lk(&ioMutex_);
    QFile f(dataPath(t)); if (!f.open(QIODevice::ReadOnly)) return false;
    if (off < 0 || off >= f.size()) return false;

    f.seek(off);
    quint32 payloadLen=0; if (f.read(reinterpret_cast<char*>(&payloadLen), sizeof(payloadLen)) != sizeof(payloadLen)) return false;
    quint32 recLen = payloadLen + sizeof(payloadLen);
    out.resize(recLen);
    f.seek(off);
    return f.read(out.data(), recLen) == recLen;
}

bool BinStorage::compactTable(const QString& t, QHash<qint64,qint64>& remap){
    QMutexLocker lk(&ioMutex_);
    const QString src = dataPath(t);
    const QString tmp = dataPath(t) + ".tmp";

    QFile in(src); if (!in.open(QIODevice::ReadOnly)) return false;
    QSaveFile out(tmp); if (!out.open(QIODevice::WriteOnly)) return false;

    remap.clear();
    qint64 rOff = 0;
    while (!in.atEnd()){
        qint64 cur = in.pos();
        quint32 payloadLen=0; if (in.read(reinterpret_cast<char*>(&payloadLen), sizeof(payloadLen)) != sizeof(payloadLen)) break;
        quint32 recLen = payloadLen + sizeof(payloadLen);

        quint8 tomb=0;
        if (in.read(reinterpret_cast<char*>(&tomb), 1) != 1) break;

        in.seek(cur);
        QByteArray rec; rec.resize(recLen);
        if (in.read(rec.data(), recLen) != recLen) break;

        if (tomb==0){
            remap.insert(cur, rOff);
            if (out.write(rec) != recLen) return false;
            rOff += recLen;
        }
        in.seek(cur + recLen);
    }

    if (!out.commit()) return false;

    QFile::remove(src);
    QFile::rename(tmp, src);

    // Reinicia free-list vacía
    FreeSpaceManager fsm; fsm.setDataPath(dataPath(t)); fsm.save(nullptr);
    return true;
}
