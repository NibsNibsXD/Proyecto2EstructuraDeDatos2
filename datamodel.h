#ifndef DATAMODEL_H
#define DATAMODEL_H

#include <QString>
#include <QList>

// Definición común del esquema de campos para TablesPage y RecordsPage
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

// Alias útil: una “tabla” es una lista de FieldDef
using Schema = QList<FieldDef>;

#endif // DATAMODEL_H
