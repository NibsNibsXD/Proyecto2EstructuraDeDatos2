#pragma once
#include <QString>
#include <QList>
#include <QByteArray>
#include <QVariant>
#include "datamodel.h"   // FieldDef / Schema (Schema = QList<FieldDef>)

class BinSerializer {
public:
    enum TypeTag : quint8 {
        TT_Int=1, TT_Float=2, TT_Bool=3, TT_CharN=4, TT_String=5, TT_DateTime=6, TT_Money=7, TT_Autonum=8
    };

    static TypeTag tagFor(const QString& normType);
    static QString normType(const QString& t);

    // Usa QList para que coincida con tu DataModel
    static QByteArray packRow(const Schema& s, const QList<QVariant>& rowValues,
                              const QList<bool>& isNull);

    static bool unpackRow(const QByteArray& rec, Schema& outSchema,
                          QList<QVariant>& outValues, QList<bool>& outNull, bool* tombstone=nullptr);

    static QByteArray packSchema(const QString& table, const Schema& s, quint32 version=1);
    static bool unpackSchema(const QByteArray& meta, QString& table, Schema& outSchema, quint32* version=nullptr);
};
