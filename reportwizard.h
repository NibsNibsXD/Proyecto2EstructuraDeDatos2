#ifndef REPORTWIZARD_H
#define REPORTWIZARD_H

#include <QDialog>
#include <QPointer>
#include <QAbstractTableModel>
#include "reportdef.h"

class QListWidget;
class QListWidgetItem;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QStackedWidget;
class QCheckBox;
class QSpinBox;

class ReportWizard : public QDialog {
    Q_OBJECT
public:
    explicit ReportWizard(QWidget* parent=nullptr);

    ReportDef result() const { return def_; }

private:
    // Paso 1: fuentes
    QWidget* buildStepSources();
    // Paso 2: campos / joins / filtros
    QWidget* buildStepFields();
    // Paso 3: grupos / orden / agregados
    QWidget* buildStepGrouping();
    // Paso 4: layout
    QWidget* buildStepLayout();

    void refreshTablesAndQueries();
    void addExtraSource();
    void addJoinRow();
    void addFieldRow(const QString& col);
    void recomputePreviewFields();
    void acceptIfValid();

private:
    QStackedWidget* stack_ = nullptr;
    QPushButton *btnPrev_=nullptr, *btnNext_=nullptr, *btnFinish_=nullptr, *btnCancel_=nullptr;

    // Step 1
    QComboBox* cbMainType_ = nullptr;
    QComboBox* cbMainValue_ = nullptr;
    QListWidget* listExtras_ = nullptr; // muestra extras añadidas
    QPushButton* btnAddExtra_ = nullptr;

    // Step 2
    QTableWidget* tblJoins_ = nullptr;
    QTableWidget* tblFields_ = nullptr;
    QLineEdit* edFilter_ = nullptr;
    QPushButton* btnAddJoin_ = nullptr;

    // Step 3
    QTableWidget* tblGroups_ = nullptr;
    QTableWidget* tblSorts_  = nullptr;
    QTableWidget* tblAggs_   = nullptr;

    // Step 4
    QComboBox* cbTemplate_ = nullptr;
    QComboBox* cbPaper_ = nullptr;
    QCheckBox* chkHeader_ = nullptr;
    QCheckBox* chkFooter_ = nullptr;
    QCheckBox* chkZebra_  = nullptr;

    ReportDef def_;
};

#endif // REPORTWIZARD_H
