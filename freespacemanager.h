#pragma once
#include <QFile>
#include <QVector>
#include <QString>
#include <functional>

/**
 * FreeSpaceManager
 * ----------------
 * Administra huecos (offset, length) en un archivo de datos.
 * - Estrategias: FirstFit / BestFit / WorstFit
 * - Persistencia: archivo sidecar "<dataPath>.free" (binario)
 * - Merge de bloques contiguos
 * - Compactación asistida por callback para reubicar registros vivos
 *
 * API de uso en DataModel:
 *  - setDataPath(path)        // path del .mad
 *  - load()/save()
 *  - setStrategy(...)
 *  - allocate(size, outOff)   // obtiene hueco o -1 => append al final
 *  - freeBlock(off, len)
 *  - fragmentationRatio()     // métrica para decidir compactar
 *  - compact(dataFile, liveRecords) // ver LiveRecord abajo
 */
class FreeSpaceManager {
public:
    enum class Strategy { FirstFit, BestFit, WorstFit };

    struct Block {
        qint64 off = 0;
        qint64 len = 0;
    };

    // Registro vivo para compactación
    // moveTo(newOff) debe:
    //   - copiar el registro si hace falta (si no lo hace el contenedor)
    //   - actualizar sus metadatos/punteros/índices
    struct LiveRecord {
        qint64 oldOff = 0;
        qint64 len    = 0;
        std::function<void(qint64 newOffset)> moveTo; // obligatorio
    };

public:
    FreeSpaceManager();

    void setDataPath(const QString& dataPath); // e.g., "/path/tabla.mad"
    bool load(QString* err = nullptr);
    bool save(QString* err = nullptr) const;

    void setStrategy(Strategy s) { strategy_ = s; }
    Strategy strategy() const { return strategy_; }

    // Reserva un bloque >= size. Devuelve true y escribe offset.
    // Si no hay hueco, devuelve false (el caller debe "append" al final).
    bool allocate(qint64 size, qint64* outOffset);

    // Libera un bloque (offset,len). Hace merge con vecinos si aplica.
    void freeBlock(qint64 off, qint64 len);

    // Métricas simples para decidir compactación.
    qint64 totalFreeBytes() const;
    qint64 largestFreeBlock() const;
    double fragmentationRatio() const; // 1 - (largestFreeBlock / totalFreeBytes) si free>0

    // Compactación “asistida”: empaqueta LiveRecords desde inicio de archivo.
    // - Recorre liveRecords en el orden dado y los ubica seguidos.
    // - Usa moveTo(newOff) para que el caller actualice sus metadatos/índices.
    // - Libera TODO el espacio entre el final del último vivo y EOF.
    // - Reescribe free list con un único hueco final (si aplica).
    bool compact(QFile& dataFile, const QVector<LiveRecord>& liveRecords,
                 QString* err = nullptr);

private:
    QString freeListPath() const;

    // util
    void insertAndMerge(Block b);
    int  pickBlockIndex(qint64 size) const; // según strategy_
    static bool byOff(const Block& a, const Block& b) { return a.off < b.off; }

private:
    QString dataPath_;
    Strategy strategy_ = Strategy::FirstFit;
    QVector<Block> free_;  // SIEMPRE ordenados por offset
};
