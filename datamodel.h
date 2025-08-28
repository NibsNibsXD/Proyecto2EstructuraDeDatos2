#ifndef DATAMODEL_H
#define DATAMODEL_H

#include <QString>
#include <QList>

// Definición de un campo de tabla (la misma que ya usabas)
struct FieldDef {
    QString name;
    QString type;      // Autonumeración, Número, Fecha/Hora, Moneda, Texto corto
    int     size = 0;
    bool    pk = false;

    // Propiedades (maqueta)
    QString formato;
    QString mascaraEntrada;
    QString titulo;
    QString valorPredeterminado;
    QString reglaValidacion;
    QString textoValidacion;
    bool    requerido = false;
    QString indexado; // No / Sí (con duplicados) / Sí (sin duplicados)
};

// Un "Schema" es simplemente la lista de FieldDef de una tabla.
using Schema = QList<FieldDef>;

#endif // DATAMODEL_H
