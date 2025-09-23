#include "freespacemanager.h"
#include <QDataStream>
#include <algorithm>

FreeSpaceManager::FreeSpaceManager() {}

void FreeSpaceManager::setDataPath(const QString& dataPath){
    dataPath_ = dataPath;
}

QString FreeSpaceManager::freeListPath() const {
    return dataPath_.isEmpty() ? QString() : (dataPath_ + ".free");
}

bool FreeSpaceManager::load(QString* err){
    free_.clear();
    const QString p = freeListPath();
    if (p.isEmpty()) { if (err) *err="DataPath vacío"; return false; }

    QFile f(p);
    if (!f.exists()) return true; // nada que cargar
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err="No se pudo abrir free-list"; return false; }
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0xFEEFCAFE;
    quint32 tag=0, ver=1, n=0;

    ds >> tag >> ver;
    if (tag != magic) { if (err)*err="Archivo .free inválido"; return false; }
    ds >> n;
    free_.resize(int(n));
    for (quint32 i=0;i<n;++i){
        qint64 off,len; ds >> off >> len;
        free_[int(i)] = {off,len};
    }
    std::sort(free_.begin(), free_.end(), byOff);
    return true;
}
bool FreeSpaceManager::save(QString* err) const{
    const QString p = freeListPath();
    if (p.isEmpty()) { if (err) *err="DataPath vacío"; return false; }
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err="No se pudo escribir free-list"; return false; }
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    const quint32 magic = 0xFEEFCAFE;
    const quint32 ver = 1;
    ds << magic << ver;
    ds << quint32(free_.size());
    for (const auto& b : free_) { ds << b.off << b.len; }
    return true;
}

void FreeSpaceManager::insertAndMerge(Block b){
    if (b.len <= 0) return;
    // insertar ordenado por offset
    int pos = 0;
    while (pos < free_.size() && free_[pos].off < b.off) ++pos;
    free_.insert(free_.begin()+pos, b);

    // merge hacia atrás
    if (pos > 0) {
        auto& prev = free_[pos-1];
        if (prev.off + prev.len == b.off){
            prev.len += b.len;
            free_.removeAt(pos);
            pos -= 1;
        }
    }
    // merge hacia adelante
    if (pos+1 < free_.size()){
        auto& cur = free_[pos];
        auto& next = free_[pos+1];
        if (cur.off + cur.len == next.off){
            cur.len += next.len;
            free_.removeAt(pos+1);
        }
    }
}

void FreeSpaceManager::freeBlock(qint64 off, qint64 len){
    if (len <= 0) return;
    insertAndMerge({off,len});
}

int FreeSpaceManager::pickBlockIndex(qint64 size) const{
    if (free_.isEmpty()) return -1;

    int bestIdx = -1;
    qint64 bestMetric = (strategy_ == Strategy::BestFit) ? LLONG_MAX : -LLONG_MAX;

    for (int i=0;i<free_.size();++i){
        const auto& b = free_[i];
        if (b.len < size) continue;

        switch(strategy_){
        case Strategy::FirstFit:
            return i;
        case Strategy::BestFit: {
            qint64 waste = b.len - size;
            if (waste < bestMetric) { bestMetric = waste; bestIdx = i; }
            break;
        }
        case Strategy::WorstFit: {
            if (b.len > bestMetric) { bestMetric = b.len; bestIdx = i; }
            break;
        }
        }
    }
    return bestIdx;
}

bool FreeSpaceManager::allocate(qint64 size, qint64* outOffset){
    if (size <= 0 || !outOffset) return false;
    int idx = pickBlockIndex(size);
    if (idx < 0) return false;

    auto b = free_[idx];
    *outOffset = b.off;

    if (b.len == size) {
        free_.removeAt(idx);
    } else {
        // split
        b.off += size;
        b.len -= size;
        free_[idx] = b;
    }
    return true;
}

qint64 FreeSpaceManager::totalFreeBytes() const{
    qint64 t=0; for (const auto& b : free_) t += b.len; return t;
}
qint64 FreeSpaceManager::largestFreeBlock() const{
    qint64 m=0; for (const auto& b : free_) if (b.len>m) m=b.len; return m;
}
double FreeSpaceManager::fragmentationRatio() const{
    qint64 tot = totalFreeBytes(); if (tot<=0) return 0.0;
    double largest = double(largestFreeBlock());
    return 1.0 - (largest / double(tot));
}

bool FreeSpaceManager::compact(QFile& dataFile,
                               const QVector<LiveRecord>& liveRecords,
                               QString* err)
{
    if (!dataFile.isOpen()){
        if (!dataFile.open(QIODevice::ReadWrite)){
            if (err) *err = "No se pudo abrir el archivo de datos para compactar";
            return false;
        }
    }

    // Paso 1: Empaquetar vivos “hacia adelante”
    qint64 writePtr = 0;
    QByteArray buf;
    buf.resize(64*1024);

    for (const auto& rec : liveRecords){
        if (!rec.moveTo) { if (err) *err="LiveRecord.moveTo vacío"; return false; }

        if (rec.oldOff == writePtr){
            // ya está donde debe
            rec.moveTo(writePtr);
            writePtr += rec.len;
            continue;
        }

        // copiar registro oldOff → writePtr
        qint64 remaining = rec.len;
        qint64 src = rec.oldOff;
        qint64 dst = writePtr;

        while (remaining > 0){
            qint64 chunk = qMin<qint64>(remaining, buf.size());
            if (dataFile.seek(src) == false) { if (err)*err="seek read falla"; return false; }
            qint64 r = dataFile.read(buf.data(), chunk);
            if (r != chunk) { if (err)*err="read incompleto"; return false; }
            if (dataFile.seek(dst) == false) { if (err)*err="seek write falla"; return false; }
            qint64 w = dataFile.write(buf.constData(), chunk);
            if (w != chunk) { if (err)*err="write incompleto"; return false; }
            src += chunk; dst += chunk; remaining -= chunk;
        }
        // notificar nuevo offset
        rec.moveTo(writePtr);
        writePtr += rec.len;
    }

    // Paso 2: truncar archivo al final del último registro vivo
    if (!dataFile.resize(writePtr)) {
        if (err) *err = "No se pudo truncar archivo tras compactar";
        return false;
    }

    // Paso 3: Recalcular free list ⇒ un único hueco a partir de writePtr?
    // (En compactación 'full' asumimos que todo lo que quedó después es basura)
    free_.clear();
    // Si quisieras dejar un bloque libre prefijado, podrías insertarlo aquí.
    // En general, tras compactar el archivo queda sin huecos internos.

    return true;
}
