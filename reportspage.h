#ifndef REPORTSPAGE_H
#define REPORTSPAGE_H

#include <QWidget>
#include <QPointer>
#include "reportdef.h"
#include "reportengine.h"
#include "reportrenderer.h"

class QListWidget;
class QTextBrowser;
class QPushButton;
class QLineEdit;

class ReportsPage : public QWidget {
    Q_OBJECT
public:
    explicit ReportsPage(QWidget* parent=nullptr);

signals:
    void savedReport(const QString& name);

public slots:
    void refreshFromModel();  // repinta lista de reportes guardados (desde DataModel)
    void openReport(const QString& name);
    void newReportWizard();
    void exportCurrentToPdf();

private:
    void renderCurrent();

private:
    QListWidget* listReports_ = nullptr;
    QTextBrowser* preview_ = nullptr;
    QPushButton* btnNew_ = nullptr;
    QPushButton* btnExportPdf_ = nullptr;

    ReportDef current_;
    ReportDataset ds_;
    ReportEngine engine_;
    ReportRenderer renderer_;
};

#endif // REPORTSPAGE_H
