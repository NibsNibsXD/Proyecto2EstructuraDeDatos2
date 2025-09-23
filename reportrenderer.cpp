#include "reportrenderer.h"
#include <QTextDocument>
#include <QtPrintSupport/QPrinter>
#include <QtPrintSupport/QPrintDialog>
#include <QPageSize>
#include <QFile>
#include <QTextCursor>
#include <QTextTable>
#include <QTextTableFormat>

ReportRenderer::ReportRenderer(QObject* parent) : QObject(parent) {}

static QString cssBase(const LayoutDef& lo) {
    const QString zebra = lo.zebra ? "tbody tr:nth-child(odd){background:#fafafa;}" : "";
    return QString(R"(
        <style>
        body{font-family:Segoe UI,Arial,sans-serif; font-size:12px; margin:0;}
        .title{font-size:16px; font-weight:bold; margin:10px 0;}
        table{border-collapse:collapse; width:100%;}
        th, td{border:1px solid #ccc; padding:4px 6px; vertical-align:middle;}
        th{background:#f0f0f0; text-align:left;}
        %1
        </style>
    )").arg(zebra);
}

QString ReportRenderer::toHtml(const ReportDef& def, const ReportDataset& ds) const {
    QStringList headers = ds.headers;
    if (!def.fields.isEmpty()) {
        headers.clear();
        for (const auto& f : def.fields)
            headers << (f.alias.isEmpty() ? f.field : f.alias);
    }

    QString h;
    h += "<html><head>";
    h += cssBase(def.layout);
    h += "</head><body>";
    if (def.layout.showHeader)
        h += QString("<div class='title'>%1</div>").arg(def.name.isEmpty() ? "Reporte" : def.name);

    h += "<table><thead><tr>";
    for (const auto& c : headers) h += "<th>" + c.toHtmlEscaped() + "</th>";
    h += "</tr></thead><tbody>";

    for (int r = 0; r < ds.rowCount(); ++r) {
        h += "<tr>";
        for (const auto& c : headers) {
            const QString v = ds.value(r, c).toString();
            h += "<td>" + v.toHtmlEscaped() + "</td>";
        }
        h += "</tr>";
    }
    h += "</tbody></table>";

    if (def.layout.showFooter)
        h += QString("<div style='margin-top:8px;color:#666;'>Filas: %1</div>").arg(ds.rowCount());

    h += "</body></html>";
    return h;
}

bool ReportRenderer::exportPdf(const QString& filePath, const ReportDef& def, const ReportDataset& ds, QString* err) const {
    QTextDocument doc;
    doc.setHtml(toHtml(def, ds));

    QPrinter printer(QPrinter::HighResolution);
    QPageSize ps(QPageSize::Letter);
    if (def.layout.paper.toLower() == "a4") ps = QPageSize::A4;
    printer.setPageSize(ps);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filePath);
    printer.setPageMargins(QMarginsF(def.layout.marginL, def.layout.marginT,
                                     def.layout.marginR, def.layout.marginB));

    bool ok = true;
    try { doc.print(&printer); }
    catch (...) { ok = false; }

    if (!ok && err) *err = "No se pudo exportar el PDF.";
    return ok;
}
