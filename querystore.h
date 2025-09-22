#pragma once
#include <QString>
#include <QStringList>
#include <QMap>

/**
 * Guarda consultas (nombre -> SQL) en un JSON "queries.json" en el cwd.
 * API muy simple, síncrona.
 */
class QueryStore {
public:
    static QueryStore& instance();

    QStringList names() const;               // nombres ordenados alfabéticamente
    bool save(const QString& name, const QString& sql, QString* err=nullptr);
    bool remove(const QString& name, QString* err=nullptr);
    bool rename(const QString& oldName, const QString& newName, QString* err=nullptr);
    QString load(const QString& name) const; // sql o ""

private:
    QueryStore();
    void loadFromDisk();
    bool saveToDisk(QString* err);

    QMap<QString, QString> store_; // ordenado por clave
};
