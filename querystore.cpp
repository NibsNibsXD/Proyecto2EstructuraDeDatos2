#include "querystore.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

static const char* kFile = "queries.json";

QueryStore& QueryStore::instance() {
    static QueryStore s;
    return s;
}

QueryStore::QueryStore() { loadFromDisk(); }

QStringList QueryStore::names() const { return store_.keys(); }

QString QueryStore::load(const QString& name) const {
    auto it = store_.find(name);
    return it == store_.end() ? QString() : it.value();
}

bool QueryStore::save(const QString& name, const QString& sql, QString* err) {
    if (name.trimmed().isEmpty()) { if (err) *err="Nombre vacío"; return false; }
    store_[name.trimmed()] = sql;
    return saveToDisk(err);
}

bool QueryStore::remove(const QString& name, QString* err) {
    if (!store_.remove(name)) return true;
    return saveToDisk(err);
}

bool QueryStore::rename(const QString& oldName, const QString& newName, QString* err) {
    if (!store_.contains(oldName)) return true;
    if (oldName == newName) return true;
    auto sql = store_.take(oldName);
    store_[newName] = sql;
    return saveToDisk(err);
}

void QueryStore::loadFromDisk() {
    store_.clear();
    QFile f(kFile);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const auto obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        if (it.value().isString()) store_.insert(it.key(), it.value().toString());
}

bool QueryStore::saveToDisk(QString* err) {
    QJsonObject obj;
    for (auto it = store_.begin(); it != store_.end(); ++it)
        obj.insert(it.key(), it.value());

    QFile f(kFile);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) *err = "No se pudo escribir queries.json";
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}
