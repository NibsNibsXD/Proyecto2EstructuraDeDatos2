#include "reportspage.h"
#include "reportwizard.h"
#include "datamodel.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QTextBrowser>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>

ReportsPage::ReportsPage(QWidget* parent) : QWidget(parent) {
    auto *hl = new QHBoxLayout(this);

    auto *left = new QWidget; auto *lv = new QVBoxLayout(left);
    listReports_ = new QListWidget;
    btnNew_ = new QPushButton("Nuevo reporte…");
    lv->addWidget(listReports_, 1);
    lv->addWidget(btnNew_);
    hl->addWidget(left, 0);

    auto *right = new QWidget; auto *rv = new QVBoxLayout(right);
    preview_ = new QTextBrowser;
    btnExportPdf_ = new QPushButton("Exportar a PDF…");
    rv->addWidget(preview_, 1);
    rv->addWidget(btnExportPdf_, 0);
    hl->addWidget(right, 1);

    connect(listReports_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it){
        if (it) openReport(it->text());
    });
    connect(btnNew_, &QPushButton::clicked, this, &ReportsPage::newReportWizard);
    connect(btnExportPdf_, &QPushButton::clicked, this, &ReportsPage::exportCurrentToPdf);

    refreshFromModel();
}

void ReportsPage::refreshFromModel() {
    listReports_->clear();
    // Intentamos: DataModel::instance().reports() -> QStringList (si existe)
    auto& dm = DataModel::instance();
    if (dm.metaObject()->indexOfMethod("reports()") >= 0) {
        QStringList names;
        bool ok = QMetaObject::invokeMethod(&dm, "reports", Qt::DirectConnection,
                                            Q_RETURN_ARG(QStringList, names));
        if (ok) for (const auto& n : names) listReports_->addItem(n);
    } else {
        // Si no hay API, no crasheamos: lista vacía
    }
}

void ReportsPage::openReport(const QString& name) {
    auto& dm = DataModel::instance();
    if (dm.metaObject()->indexOfMethod("reportJson(QString)") < 0) {
        QMessageBox::warning(this, "Reportes","El DataModel no expone reportJson(name).");
        return;
    }

    QJsonObject jobj;
    bool ok = QMetaObject::invokeMethod(&dm, "reportJson", Qt::DirectConnection,
                                        Q_RETURN_ARG(QJsonObject, jobj),
                                        Q_ARG(QString, name));
    if (!ok || jobj.isEmpty()) {
        QMessageBox::warning(this, "Reportes","No se pudo cargar el reporte.");
        return;
    }

    current_ = ReportDef::fromJson(jobj);
    renderCurrent();
}

void ReportsPage::renderCurrent() {
    ds_.clear();
    QString err;
    if (!engine_.build(current_, &ds_, &err)) {
        preview_->setHtml(QString("<html><body><div style='color:#900;'>%1</div></body></html>").arg(err.toHtmlEscaped()));
        return;
    }
    preview_->setHtml(renderer_.toHtml(current_, ds_));
}

void ReportsPage::newReportWizard() {
    ReportWizard wiz(this);
    if (wiz.exec() != QDialog::Accepted) return;
    current_ = wiz.result();

    // Guardar en DataModel (si existe API)
    auto& dm = DataModel::instance();
    if (dm.metaObject()->indexOfMethod("saveReport(QString,QJsonObject)") >= 0) {
        QJsonObject o = current_.toJson();
        bool ok = QMetaObject::invokeMethod(&dm, "saveReport", Qt::DirectConnection,
                                            Q_RETURN_ARG(bool, ok),
                                            Q_ARG(QString, current_.name),
                                            Q_ARG(QJsonObject, o));
        Q_UNUSED(ok);
    }

    refreshFromModel();
    renderCurrent();
    emit savedReport(current_.name);
}

void ReportsPage::exportCurrentToPdf() {
    if (current_.name.isEmpty()) { QMessageBox::warning(this, "PDF", "No hay reporte activo."); return; }
    const QString file = QFileDialog::getSaveFileName(this, "Exportar PDF", current_.name + ".pdf", "PDF (*.pdf)");
    if (file.isEmpty()) return;
    QString err;
    if (!renderer_.exportPdf(file, current_, ds_, &err)) {
        QMessageBox::warning(this, "PDF", err);
    } else {
        QMessageBox::information(this, "PDF", "Exportado correctamente.");
    }
}
