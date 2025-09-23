#include "reportwizard.h"
#include "datamodel.h"
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QCheckBox>
#include <QMessageBox>

static QTableWidget* makeGrid(int cols, const QStringList& headers) {
    auto *t = new QTableWidget(0, cols);
    t->setAlternatingRowColors(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    t->horizontalHeader()->setStretchLastSection(true);
    for (int i=0;i<headers.size();++i)
        t->setHorizontalHeaderItem(i, new QTableWidgetItem(headers[i]));
    return t;
}

ReportWizard::ReportWizard(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Report Wizard");
    resize(840, 620);

    auto *root = new QVBoxLayout(this);
    stack_ = new QStackedWidget;
    root->addWidget(stack_, 1);

    // botones
    auto *btns = new QHBoxLayout;
    btnPrev_   = new QPushButton("Anterior");
    btnNext_   = new QPushButton("Siguiente");
    btnFinish_ = new QPushButton("Finalizar");
    btnCancel_ = new QPushButton("Cancelar");
    btnFinish_->setEnabled(false);
    btnPrev_->setEnabled(false);
    btns->addStretch();
    btns->addWidget(btnPrev_);
    btns->addWidget(btnNext_);
    btns->addWidget(btnFinish_);
    btns->addWidget(btnCancel_);
    root->addLayout(btns);

    stack_->addWidget(buildStepSources());
    stack_->addWidget(buildStepFields());
    stack_->addWidget(buildStepGrouping());
    stack_->addWidget(buildStepLayout());

    connect(btnPrev_, &QPushButton::clicked, this, [this]{
        int ix = stack_->currentIndex();
        if (ix > 0) { stack_->setCurrentIndex(ix-1); }
        btnPrev_->setEnabled(stack_->currentIndex()>0);
        btnNext_->setEnabled(true);
        btnFinish_->setEnabled(stack_->currentIndex()==(stack_->count()-1));
    });
    connect(btnNext_, &QPushButton::clicked, this, [this]{
        int ix = stack_->currentIndex();
        if (ix < stack_->count()-1) stack_->setCurrentIndex(ix+1);
        btnPrev_->setEnabled(stack_->currentIndex()>0);
        btnNext_->setEnabled(stack_->currentIndex()<stack_->count()-1);
        btnFinish_->setEnabled(stack_->currentIndex()==(stack_->count()-1));
    });
    connect(btnCancel_, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnFinish_, &QPushButton::clicked, this, &ReportWizard::acceptIfValid);

    refreshTablesAndQueries();

    // defaults
    def_.layout.templateName = "tabular";
    def_.layout.paper = "Letter";
}

QWidget* ReportWizard::buildStepSources() {
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);

    auto *h1 = new QHBoxLayout;
    cbMainType_  = new QComboBox;
    cbMainType_->addItems({"Table","Query","SQL"});
    cbMainValue_ = new QComboBox;
    cbMainValue_->setEditable(true); // para SQL crudo
    h1->addWidget(new QLabel("Origen principal:"));
    h1->addWidget(cbMainType_, 0);
    h1->addWidget(cbMainValue_, 1);

    auto *h2 = new QHBoxLayout;
    listExtras_  = new QListWidget;
    listExtras_->setMinimumHeight(120);
    btnAddExtra_ = new QPushButton("Agregar fuente extra");
    auto *btnDelExtra = new QPushButton("Quitar seleccionada");
    auto *extraBtns = new QVBoxLayout;
    extraBtns->addWidget(btnAddExtra_);
    extraBtns->addWidget(btnDelExtra);
    extraBtns->addStretch();
    h2->addWidget(new QLabel("Fuentes adicionales (para comparaciones/joins):"), 0);
    v->addLayout(h1);
    v->addLayout(h2);
    auto *h3 = new QHBoxLayout;
    h3->addWidget(listExtras_, 1);
    h3->addLayout(extraBtns, 0);
    v->addLayout(h3);

    connect(btnAddExtra_, &QPushButton::clicked, this, &ReportWizard::addExtraSource);
    connect(btnDelExtra, &QPushButton::clicked, this, [this]{
        delete listExtras_->takeItem(listExtras_->currentRow());
    });

    return w;
}

QWidget* ReportWizard::buildStepFields() {
    auto *w = new QWidget; auto *v = new QVBoxLayout(w);

    // joins
    v->addWidget(new QLabel("Joins / Comparaciones (en cadena):"));
    tblJoins_ = makeGrid(6, {"Left Source","Left Field","Right Source","Right Field","Left Join?","Prefix Right"});
    v->addWidget(tblJoins_);
    btnAddJoin_ = new QPushButton("Agregar join");
    auto *btnDelJoin = new QPushButton("Quitar join");
    auto *hb = new QHBoxLayout; hb->addStretch(); hb->addWidget(btnAddJoin_); hb->addWidget(btnDelJoin);
    v->addLayout(hb);
    connect(btnAddJoin_, &QPushButton::clicked, this, &ReportWizard::addJoinRow);
    connect(btnDelJoin, &QPushButton::clicked, this, [this]{ int r=tblJoins_->currentRow(); if(r>=0) tblJoins_->removeRow(r); });

    // fields
    v->addWidget(new QLabel("Campos a mostrar:"));
    tblFields_ = makeGrid(3, {"Campo","Alias","Ancho"});
    v->addWidget(tblFields_);
    auto *btnAddField = new QPushButton("Agregar campo…");
    auto *btnDelField = new QPushButton("Quitar campo");
    auto *hb2 = new QHBoxLayout; hb2->addStretch(); hb2->addWidget(btnAddField); hb2->addWidget(btnDelField);
    v->addLayout(hb2);
    connect(btnAddField, &QPushButton::clicked, this, [this]{ addFieldRow(""); });
    connect(btnDelField, &QPushButton::clicked, this, [this]{ int r=tblFields_->currentRow(); if(r>=0) tblFields_->removeRow(r); });

    // filtros
    v->addWidget(new QLabel("Filtros (uno por línea):"));
    edFilter_ = new QLineEdit; edFilter_->setPlaceholderText("Ej: Monto>100; Fecha BETWEEN '2024-01-01' AND '2024-12-31'");
    v->addWidget(edFilter_);

    return w;
}

QWidget* ReportWizard::buildStepGrouping() {
    auto *w = new QWidget; auto *v = new QVBoxLayout(w);

    v->addWidget(new QLabel("Agrupar por:"));
    tblGroups_ = makeGrid(3, {"Campo","Orden","Salto de página"});
    v->addWidget(tblGroups_);
    auto *h1 = new QHBoxLayout;
    auto *btnAddG = new QPushButton("Agregar grupo");
    auto *btnDelG = new QPushButton("Quitar grupo");
    h1->addStretch(); h1->addWidget(btnAddG); h1->addWidget(btnDelG);
    v->addLayout(h1);
    connect(btnAddG, &QPushButton::clicked, this, [this]{
        int r = tblGroups_->rowCount(); tblGroups_->insertRow(r);
        tblGroups_->setItem(r, 0, new QTableWidgetItem(""));
        tblGroups_->setItem(r, 1, new QTableWidgetItem("asc"));
        tblGroups_->setItem(r, 2, new QTableWidgetItem("no"));
    });
    connect(btnDelG, &QPushButton::clicked, this, [this]{ int r=tblGroups_->currentRow(); if(r>=0) tblGroups_->removeRow(r); });

    v->addWidget(new QLabel("Ordenar por:"));
    tblSorts_ = makeGrid(2, {"Campo","Orden"});
    v->addWidget(tblSorts_);
    auto *h2 = new QHBoxLayout;
    auto *btnAddS = new QPushButton("Agregar orden");
    auto *btnDelS = new QPushButton("Quitar orden");
    h2->addStretch(); h2->addWidget(btnAddS); h2->addWidget(btnDelS);
    v->addLayout(h2);
    connect(btnAddS, &QPushButton::clicked, this, [this]{
        int r = tblSorts_->rowCount(); tblSorts_->insertRow(r);
        tblSorts_->setItem(r, 0, new QTableWidgetItem(""));
        tblSorts_->setItem(r, 1, new QTableWidgetItem("asc"));
    });
    connect(btnDelS, &QPushButton::clicked, this, [this]{ int r=tblSorts_->currentRow(); if(r>=0) tblSorts_->removeRow(r); });

    v->addWidget(new QLabel("Totales / Agregados:"));
    tblAggs_ = makeGrid(3, {"Campo","Función (sum/count/avg/min/max)","Ámbito (report o group:<campo>)"});
    v->addWidget(tblAggs_);
    auto *h3 = new QHBoxLayout;
    auto *btnAddA = new QPushButton("Agregar agregado");
    auto *btnDelA = new QPushButton("Quitar agregado");
    h3->addStretch(); h3->addWidget(btnAddA); h3->addWidget(btnDelA);
    v->addLayout(h3);
    connect(btnAddA, &QPushButton::clicked, this, [this]{
        int r = tblAggs_->rowCount(); tblAggs_->insertRow(r);
        tblAggs_->setItem(r, 0, new QTableWidgetItem(""));
        tblAggs_->setItem(r, 1, new QTableWidgetItem("sum"));
        tblAggs_->setItem(r, 2, new QTableWidgetItem("report"));
    });
    connect(btnDelA, &QPushButton::clicked, this, [this]{ int r=tblAggs_->currentRow(); if(r>=0) tblAggs_->removeRow(r); });

    return w;
}

QWidget* ReportWizard::buildStepLayout() {
    auto *w = new QWidget; auto *v = new QVBoxLayout(w);

    auto *h1 = new QHBoxLayout;
    cbTemplate_ = new QComboBox; cbTemplate_->addItems({"tabular","cards"});
    cbPaper_    = new QComboBox; cbPaper_->addItems({"Letter","A4"});
    chkHeader_  = new QCheckBox("Mostrar encabezado");
    chkFooter_  = new QCheckBox("Mostrar pie");
    chkZebra_   = new QCheckBox("Filas cebra");
    chkHeader_->setChecked(true);
    chkFooter_->setChecked(true);
    chkZebra_->setChecked(true);
    h1->addWidget(new QLabel("Plantilla:")); h1->addWidget(cbTemplate_);
    h1->addSpacing(10);
    h1->addWidget(new QLabel("Papel:")); h1->addWidget(cbPaper_);
    h1->addSpacing(10);
    h1->addWidget(chkHeader_);
    h1->addSpacing(10);
    h1->addWidget(chkFooter_);
    h1->addSpacing(10);
    h1->addWidget(chkZebra_);

    v->addLayout(h1);
    v->addStretch();
    v->addWidget(new QLabel("Pulsa Finalizar para crear el reporte."));

    return w;
}

void ReportWizard::refreshTablesAndQueries() {
    auto& dm = DataModel::instance();
    cbMainValue_->clear();
    const auto tabs = dm.tables();
    const auto qs   = dm.queries();

    // por defecto arrancamos con tabla si hay
    cbMainType_->setCurrentIndex(0);
    for (const auto& t : tabs) cbMainValue_->addItem(t);
    if (cbMainValue_->count()==0) {
        // si no hay tablas, mostrar queries
        cbMainType_->setCurrentIndex(1);
        for (const auto& q : qs) cbMainValue_->addItem(q);
    }

    connect(cbMainType_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, &dm](int ix){
        cbMainValue_->clear();
        if (ix==0) { for (auto& t : dm.tables()) cbMainValue_->addItem(t); }
        else if (ix==1) { for (auto& q : dm.queries()) cbMainValue_->addItem(q); }
        else { /* SQL crudo: editable */ }
    });
}

void ReportWizard::addExtraSource() {
    // Diálogo mínimo inline: elegimos tipo+valor y lo agregamos a lista
    auto& dm = DataModel::instance();
    QString label = "table:";
    if (!dm.tables().isEmpty()) label += dm.tables().first();
    listExtras_->addItem(label);
}

void ReportWizard::addJoinRow() {
    int r = tblJoins_->rowCount(); tblJoins_->insertRow(r);
    tblJoins_->setItem(r, 0, new QTableWidgetItem("main"));
    tblJoins_->setItem(r, 1, new QTableWidgetItem(""));
    tblJoins_->setItem(r, 2, new QTableWidgetItem("s1")); // primera extra
    tblJoins_->setItem(r, 3, new QTableWidgetItem(""));
    tblJoins_->setItem(r, 4, new QTableWidgetItem("true"));
    tblJoins_->setItem(r, 5, new QTableWidgetItem("r_"));
}

void ReportWizard::addFieldRow(const QString& col) {
    int r = tblFields_->rowCount(); tblFields_->insertRow(r);
    tblFields_->setItem(r, 0, new QTableWidgetItem(col));
    tblFields_->setItem(r, 1, new QTableWidgetItem(col));
    tblFields_->setItem(r, 2, new QTableWidgetItem("120"));
}

void ReportWizard::acceptIfValid() {
    // Paso 1 → def_.mainSource y extras
    const int tix = cbMainType_->currentIndex();
    if (tix==0) def_.mainSource.type = ReportSourceType::Table;
    else if (tix==1) def_.mainSource.type = ReportSourceType::Query;
    else def_.mainSource.type = ReportSourceType::Sql;

    def_.mainSource.nameOrSql = cbMainValue_->currentText().trimmed();
    if (def_.mainSource.nameOrSql.isEmpty()) {
        QMessageBox::warning(this, "Wizard", "Debes indicar un origen principal.");
        return;
    }

    def_.extraSources.clear();
    for (int i=0;i<listExtras_->count();++i) {
        const QString text = listExtras_->item(i)->text();
        ReportSource s;
        if (text.startsWith("table:")) {
            s.type = ReportSourceType::Table; s.nameOrSql = text.mid(6).trimmed();
        } else if (text.startsWith("query:")) {
            s.type = ReportSourceType::Query; s.nameOrSql = text.mid(6).trimmed();
        } else if (text.startsWith("sql:")) {
            s.type = ReportSourceType::Sql; s.nameOrSql = text.mid(4).trimmed();
        } else {
            // por defecto tabla
            s.type = ReportSourceType::Table; s.nameOrSql = text.trimmed();
        }
        if (!s.nameOrSql.isEmpty()) def_.extraSources.push_back(s);
    }

    // Paso 2 → joins/fields/filters
    def_.joins.clear();
    for (int r=0;r<tblJoins_->rowCount();++r) {
        JoinDef j;
        j.leftSource  = tblJoins_->item(r,0)? tblJoins_->item(r,0)->text().trimmed() : "main";
        j.leftField   = tblJoins_->item(r,1)? tblJoins_->item(r,1)->text().trimmed() : "";
        j.rightSource = tblJoins_->item(r,2)? tblJoins_->item(r,2)->text().trimmed() : "";
        j.rightField  = tblJoins_->item(r,3)? tblJoins_->item(r,3)->text().trimmed() : "";
        j.leftJoin    = (tblJoins_->item(r,4)? tblJoins_->item(r,4)->text().trimmed().toLower()=="true" : true);
        j.prefixRight = tblJoins_->item(r,5)? tblJoins_->item(r,5)->text().trimmed() : "r_";
        if (!j.leftField.isEmpty() && !j.rightField.isEmpty() && !j.rightSource.isEmpty())
            def_.joins.push_back(j);
    }

    def_.fields.clear();
    for (int r=0;r<tblFields_->rowCount();++r) {
        ReportField f;
        f.field = tblFields_->item(r,0)? tblFields_->item(r,0)->text().trimmed() : "";
        f.alias = tblFields_->item(r,1)? tblFields_->item(r,1)->text().trimmed() : f.field;
        f.width = tblFields_->item(r,2)? tblFields_->item(r,2)->text().toInt() : 120;
        if (!f.field.isEmpty()) def_.fields.push_back(f);
    }

    def_.filters.clear();
    const QString all = edFilter_->text().trimmed();
    for (const auto& line : all.split(";", Qt::SkipEmptyParts)) {
        FilterDef fd; fd.expr = line.trimmed();
        if (!fd.expr.isEmpty()) def_.filters.push_back(fd);
    }

    // Paso 3 → groups/sorts/aggs
    def_.groups.clear();
    for (int r=0;r<tblGroups_->rowCount();++r) {
        GroupDef g;
        g.field = tblGroups_->item(r,0)? tblGroups_->item(r,0)->text().trimmed() : "";
        g.order = (tblGroups_->item(r,1) && tblGroups_->item(r,1)->text().toLower()=="desc")
                      ? Qt::DescendingOrder : Qt::AscendingOrder;
        g.pageBreakAfter = (tblGroups_->item(r,2) && tblGroups_->item(r,2)->text().toLower().startsWith("s"));
        if (!g.field.isEmpty()) def_.groups.push_back(g);
    }

    def_.sorts.clear();
    for (int r=0;r<tblSorts_->rowCount();++r) {
        SortDef s;
        s.field = tblSorts_->item(r,0)? tblSorts_->item(r,0)->text().trimmed() : "";
        s.order = (tblSorts_->item(r,1) && tblSorts_->item(r,1)->text().toLower()=="desc")
                      ? Qt::DescendingOrder : Qt::AscendingOrder;
        if (!s.field.isEmpty()) def_.sorts.push_back(s);
    }

    def_.aggregates.clear();
    for (int r=0;r<tblAggs_->rowCount();++r) {
        AggDef a;
        a.field = tblAggs_->item(r,0)? tblAggs_->item(r,0)->text().trimmed() : "";
        a.fn    = aggFromString(tblAggs_->item(r,1)? tblAggs_->item(r,1)->text().trimmed() : "none");
        a.scope = tblAggs_->item(r,2)? tblAggs_->item(r,2)->text().trimmed() : "report";
        if (!a.field.isEmpty() && a.fn != AggFn::None) def_.aggregates.push_back(a);
    }

    // Paso 4 → layout
    def_.layout.templateName = cbTemplate_->currentText();
    def_.layout.paper        = cbPaper_->currentText();
    def_.layout.showHeader   = chkHeader_->isChecked();
    def_.layout.showFooter   = chkFooter_->isChecked();
    def_.layout.zebra        = chkZebra_->isChecked();

    if (def_.name.isEmpty()) def_.name = "NuevoReporte";
    accept();
}
