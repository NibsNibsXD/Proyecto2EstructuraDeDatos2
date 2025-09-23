#include "binserializer.h"
#include <QDataStream>
#include <QDateTime>
#include <QIODevice>

static void writeStr(QDataStream& ds, const QString& s){
    const QByteArray u = s.toUtf8();
    ds << quint16(u.size());
    if (!u.isEmpty()) ds.writeRawData(u.constData(), u.size());
}
static QString readStr(QDataStream& ds){
    quint16 n=0; ds >> n;
    QByteArray u; u.resize(n);
    if (n) ds.readRawData(u.data(), n);
    return QString::fromUtf8(u);
}

QString BinSerializer::normType(const QString& t){
    const QString s = t.trimmed().toLower();
    if (s.startsWith(u"auto"))                      return "autonumeracion";
    if (s.startsWith(u"número")||s.startsWith(u"numero")) return "numero";
    if (s.startsWith(u"fecha"))                     return "fecha_hora";
    if (s.startsWith(u"moneda"))                    return "moneda";
    if (s.startsWith(u"sí/no")||s.startsWith(u"si/no"))   return "booleano";
    if (s.startsWith(u"texto largo"))               return "texto_largo";
    return "texto";
}

BinSerializer::TypeTag BinSerializer::tagFor(const QString& nt){
    const QString t = nt.trimmed().toLower();
    if (t=="autonumeracion") return TT_Autonum;
    if (t=="numero")         return TT_Float;
    if (t=="fecha_hora")     return TT_DateTime;
    if (t=="moneda")         return TT_Money;
    if (t=="booleano")       return TT_Bool;
    if (t=="texto_largo")    return TT_String;
    return TT_String;
}

QByteArray BinSerializer::packRow(const Schema& s, const QList<QVariant>& rowValues,
                                  const QList<bool>& isNull)
{
    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);

    const int n = s.size();
    ds << quint8(0);           // tombstone = 0 (vivo)
    ds << quint16(n);

    const int nb = (n + 7) / 8;
    QByteArray bitmap(nb, 0);
    for (int i=0;i<n;++i) if (i < isNull.size() && isNull[i]) bitmap[i/8] |= (1u << (i%8));
    if (nb) ds.writeRawData(bitmap.constData(), nb);

    for (int i=0;i<n;++i){
        writeStr(ds, s[i].name);
        const auto tag = tagFor(normType(s[i].type));
        ds << quint8(tag);

        QByteArray val;
        {
            QDataStream dv(&val, QIODevice::WriteOnly);
            dv.setByteOrder(QDataStream::LittleEndian);
            if (i < rowValues.size() && !(i < isNull.size() && isNull[i])) {
                const QVariant& v = rowValues[i];
                switch(tag){
                case TT_Bool:    dv << quint8(v.toBool() ? 1:0); break;
                case TT_Autonum:
                case TT_Int:     dv << qint64(v.toLongLong()); break;
                case TT_Float:   dv << double(v.toDouble()); break;
                case TT_Money:   dv << double(v.toDouble()); break;
                case TT_DateTime: dv << v.toDateTime().toMSecsSinceEpoch(); break;
                case TT_CharN:
                case TT_String:
                default:         writeStr(dv, v.toString()); break;
                }
            }
        }
        ds << quint32(val.size());
        if (!val.isEmpty()) ds.writeRawData(val.constData(), val.size());
    }

    QByteArray out;
    QDataStream doo(&out, QIODevice::WriteOnly);
    doo.setByteOrder(QDataStream::LittleEndian);
    doo << quint32(payload.size());
    out.append(payload);
    return out;
}

bool BinSerializer::unpackRow(const QByteArray& rec, Schema& outSchema,
                              QList<QVariant>& outValues, QList<bool>& outNull, bool* tombstone)
{
    QDataStream ds(rec);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 payloadLen=0; ds >> payloadLen;
    if (payloadLen+sizeof(quint32) != (quint32)rec.size()) return false;

    quint8 tomb=0; ds >> tomb;
    if (tombstone) *tombstone = (tomb!=0);

    quint16 n=0; ds >> n;
    outValues.resize(n); outNull.resize(n); outSchema.resize(n);

    const int nb = (n+7)/8;
    QByteArray bm(nb, 0); if (nb) ds.readRawData(bm.data(), nb);

    for (int i=0;i<n;++i){
        FieldDef f;
        f.name = readStr(ds);
        quint8 tag=0; ds >> tag;
        quint32 vlen=0; ds >> vlen;
        QByteArray v; v.resize(vlen);
        if (vlen) ds.readRawData(v.data(), vlen);

        outSchema[i]=f;
        outNull[i] = (bm[i/8] & (1u<<(i%8))) != 0;

        if (outNull[i]) { outValues[i]=QVariant(); continue; }
        QDataStream dv(v); dv.setByteOrder(QDataStream::LittleEndian);
        switch(tag){
        case TT_Bool: { quint8 b=0; dv>>b; outValues[i]=bool(b!=0); break; }
        case TT_Autonum:
        case TT_Int:  { qint64 x=0; dv>>x; outValues[i]=qlonglong(x); break; }
        case TT_Float:
        case TT_Money:{ double d=0; dv>>d; outValues[i]=d; break; }
        case TT_DateTime:{ qint64 ms=0; dv>>ms; outValues[i]=QDateTime::fromMSecsSinceEpoch(ms); break; }
        default: { outValues[i]=readStr(dv); break; }
        }
    }
    return true;
}

QByteArray BinSerializer::packSchema(const QString& table, const Schema& s, quint32 ver){
    QByteArray out; QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << quint32(0xB10F5C4E);
    ds << quint32(ver);
    ds << quint16(s.size());
    const QByteArray t = table.toUtf8();
    ds << quint16(t.size()); if (t.size()) ds.writeRawData(t.constData(), t.size());
    for (const auto& f : s){
        const QByteArray n = f.name.toUtf8();
        ds << quint16(n.size()); if (n.size()) ds.writeRawData(n.constData(), n.size());
        const QByteArray ty = f.type.toUtf8();
        ds << quint16(ty.size()); if (ty.size()) ds.writeRawData(ty.constData(), ty.size());
        ds << quint8(f.pk ? 1:0) << quint8(f.requerido ? 1:0);
    }
    return out;
}

bool BinSerializer::unpackSchema(const QByteArray& meta, QString& table, Schema& out, quint32* ver){
    QDataStream ds(meta); ds.setByteOrder(QDataStream::LittleEndian);
    quint32 magic=0, v=0; ds>>magic; if (magic!=0xB10F5C4E) return false; ds>>v;
    if (ver) *ver=v;
    quint16 n=0; ds>>n;
    quint16 tn=0; ds>>tn;
    QByteArray tb; tb.resize(tn); if (tn) ds.readRawData(tb.data(), tn);
    table = QString::fromUtf8(tb);
    out.clear(); out.reserve(n);
    for (int i=0;i<n;++i){
        FieldDef f;
        quint16 ln=0; ds>>ln; QByteArray lnB(ln,0); if (ln) ds.readRawData(lnB.data(), ln);
        f.name = QString::fromUtf8(lnB);
        quint16 ty=0; ds>>ty; QByteArray tyB(ty,0); if (ty) ds.readRawData(tyB.data(), ty);
        f.type = QString::fromUtf8(tyB);
        quint8 pk=0, req=0; ds>>pk>>req; f.pk = pk!=0; f.requerido = req!=0;
        out.push_back(f);
    }
    return true;
}
