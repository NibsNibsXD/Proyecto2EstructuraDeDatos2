#ifndef REPORTRENDERER_H
#define REPORTRENDERER_H

#include <QObject>
#include <QString>
#include <QTextDocument>
#include "reportdef.h"
#include "reportengine.h"

class ReportRenderer : public QObject {
    Q_OBJECT
public:
    explicit ReportRenderer(QObject* parent=nullptr);

    // Genera HTML listo para previsualizar
    QString toHtml(const ReportDef& def, const ReportDataset& ds) const;

    // Exporta a PDF usando QTextDocument
    bool exportPdf(const QString& filePath, const ReportDef& def, const ReportDataset& ds, QString* err=nullptr) const;
};

#endif // REPORTRENDERER_H
