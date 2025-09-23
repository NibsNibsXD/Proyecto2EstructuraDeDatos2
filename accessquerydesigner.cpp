#include "accessquerydesigner.h"
#include "datamodel.h"
#include "querystore.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QToolButton>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QInputDialog>

static QToolButton* mkBtn(const QString& text){
    auto *b = new QToolButton;
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return b;
}

AccessQueryDesignerPage::AccessQueryDesignerPage(QWidget* parent) : QWidget(parent){
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(8,8,8,8);
    root->setSpacing(6);

    // ===== Barra superior =====
    auto top = new QHBoxLayout; top->setSpacing(8);
    top->addWidget(new QLabel("Tabla:"));
    cbTable_ = new QComboBox; cbTable_->addItems(DataModel::instance().tables());
    top->addWidget(cbTable_, 0);

    top->addSpacing(12);
    top->addWidget(new QLabel("Nombre:"));
    edName_ = new QLineEdit; edName_->setPlaceholderText("Sin nombre");
    top->addWidget(edName_, 1);

    auto bRun    = mkBtn("Ejecutar");
    auto bSave   = mkBtn("Guardar");
    auto bSaveAs = mkBtn("Guardar como…");
    auto bRen    = mkBtn("Renombrar…");
    auto bDel    = mkBtn("Eliminar");
    top->addSpacing(12);
    top->addWidget(bRun); top->addWidget(bSave); top->addWidget(bSaveAs);
    top->addWidget(bRen); top->addWidget(bDel);

    // ===== Centro: Campos (izq) + Grid tipo Access (der) =====
    auto mid = new QHBoxLayout; mid->setSpacing(10);

    // Izquierda: lista de campos
    auto leftCol = new QVBoxLayout;
    leftCol->addWidget(new QLabel("Campos"));
    lwFields_ = new QListWidget; lwFields_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    leftCol->addWidget(lwFields_, 1);
    auto bAdd = mkBtn("Añadir al grid →");
    leftCol->addWidget(bAdd);

    // Derecha: herramientas + grid
    auto rightCol = new QVBoxLayout;

    // Herramientas (mover/quitar/operadores)
    auto tools = new QHBoxLayout;
    auto bLeft  = mkBtn("←");
    auto bRight = mkBtn("→");
    auto bRemove= mkBtn("Quitar col");
    auto bClear = mkBtn("Limpiar grid");
    tools->addWidget(bLeft); tools->addWidget(bRight); tools->addWidget(bRemove); tools->addWidget(bClear);
    tools->addSpacing(18);
    tools->addWidget(new QLabel("Operadores:"));
    auto bEq = mkBtn("="), bNe = mkBtn("<>"), bGt = mkBtn(">"), bLt = mkBtn("<"),
        bGe = mkBtn(">="), bLe = mkBtn("<="), bLike = mkBtn("LIKE"),
        bBetween = mkBtn("BETWEEN"), bIn = mkBtn("IN (…)"),
        bIsNull = mkBtn("IS NULL"), bNotNull = mkBtn("IS NOT NULL"),
        bTrue = mkBtn("TRUE"), bFalse = mkBtn("FALSE");
    for (auto *b : {bEq,bNe,bGt,bLt,bGe,bLe,bLike,bBetween,bIn,bIsNull,bNotNull,bTrue,bFalse}) tools->addWidget(b);

    // Grid: 2 filas fijas (Criteria / Or), columnas dinámicas por campo añadido
    grid_ = new QTableWidget(2, 0);
    grid_->setHorizontalHeaderLabels(QStringList()); // headers = nombres de campo al añadir
    grid_->horizontalHeader()->setStretchLastSection(true);
    grid_->verticalHeader()->setVisible(true);
    grid_->setVerticalHeaderLabels(QStringList() << "Criteria" << "Or");
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);
    grid_->setSelectionBehavior(QAbstractItemView::SelectItems);
    grid_->setEditTriggers(QAbstractItemView::AllEditTriggers);

    // Limit + preview
    auto bottom = new QHBoxLayout;
    bottom->addWidget(new QLabel("LIMIT:"));
    spLimit_ = new QSpinBox; spLimit_->setRange(0, 1000000); spLimit_->setValue(0);
    bottom->addWidget(spLimit_, 0);
    bottom->addSpacing(12);
    bottom->addWidget(new QLabel("Preview SQL:"));
    sqlPreview_ = new QLabel; sqlPreview_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sqlPreview_->setStyleSheet("font-family:monospace; color:#444;");
    bottom->addWidget(sqlPreview_, 1);

    status_ = new QLabel; status_->setStyleSheet("color:#666");

    auto rightWrap = new QVBoxLayout;
    rightWrap->addLayout(tools);
    rightWrap->addWidget(grid_, 1);
    rightWrap->addLayout(bottom);

    mid->addLayout(leftCol, 0);
    mid->addLayout(rightWrap, 1);

    root->addLayout(top);
    root->addLayout(mid, 1);
    root->addWidget(status_);

    // Conexiones
    connect(cbTable_, &QComboBox::currentTextChanged, this, &AccessQueryDesignerPage::onTableChanged);
    connect(lwFields_, &QListWidget::itemDoubleClicked, [this](QListWidgetItem*){ onAddSelectedField(); });
    connect(bAdd, &QToolButton::clicked, this, &AccessQueryDesignerPage::onAddSelectedField);

    connect(bRemove, &QToolButton::clicked, this, &AccessQueryDesignerPage::onRemoveSelectedColumn);
    connect(bLeft,   &QToolButton::clicked, this, &AccessQueryDesignerPage::onMoveLeft);
    connect(bRight,  &QToolButton::clicked, this, &AccessQueryDesignerPage::onMoveRight);
    connect(bClear,  &QToolButton::clicked, this, &AccessQueryDesignerPage::onClearGrid);

    connect(bEq, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpEq);
    connect(bNe, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpNe);
    connect(bGt, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpGt);
    connect(bLt, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpLt);
    connect(bGe, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpGe);
    connect(bLe, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpLe);
    connect(bLike, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpLike);
    connect(bBetween, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpBetween);
    connect(bIn, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpIn);
    connect(bIsNull, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertIsNull);
    connect(bNotNull, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertNotNull);
    connect(bTrue, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertTrue);
    connect(bFalse, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertFalse);

    connect(bRun,    &QToolButton::clicked, this, &AccessQueryDesignerPage::onRun);
    connect(bSave,   &QToolButton::clicked, this, &AccessQueryDesignerPage::onSave);
    connect(bSaveAs, &QToolButton::clicked, this, &AccessQueryDesignerPage::onSaveAs);
    connect(bRen,    &QToolButton::clicked, this, &AccessQueryDesignerPage::onRename);
    connect(bDel,    &QToolButton::clicked, this, &AccessQueryDesignerPage::onDelete);

    // Inicial
    onTableChanged(cbTable_->currentText());
}

void AccessQueryDesignerPage::setName(const QString& name){ edName_->setText(name); }
void AccessQueryDesignerPage::setSqlText(const QString& sql){ lastSqlText_ = sql; sqlPreview_->setText(sql); }
QString AccessQueryDesignerPage::currentTable() const { return cbTable_->currentText(); }

void AccessQueryDesignerPage::rebuildFields(){
    lwFields_->clear();
    const Schema s = DataModel::instance().schema(currentTable());
    for (const auto& f : s) lwFields_->addItem(f.name);
}

void AccessQueryDesignerPage::onTableChanged(const QString&){
    rebuildFields();
    onClearGrid();
}

void AccessQueryDesignerPage::onAddSelectedField(){
    const auto sel = lwFields_->selectedItems();
    if (sel.isEmpty()) return;
    for (auto *it : sel) {
        const int col = grid_->columnCount();
        grid_->insertColumn(col);
        grid_->setHorizontalHeaderItem(col, new QTableWidgetItem(it->text())); // Field name
        // Creamos celdas vacías Criteria/Or para que se puedan editar
        for (int r=0;r<grid_->rowCount();++r){
            if (!grid_->item(r,col)) grid_->setItem(r,col,new QTableWidgetItem);
        }
    }
    sqlPreview_->setText(buildSql());
}

void AccessQueryDesignerPage::onRemoveSelectedColumn(){
    auto items = grid_->selectedItems();
    if (items.isEmpty()) return;
    int col = items.first()->column();
    grid_->removeColumn(col);
    sqlPreview_->setText(buildSql());
}
void AccessQueryDesignerPage::onMoveLeft(){
    auto items = grid_->selectedItems(); if (items.isEmpty()) return;
    int col = items.first()->column(); if (col<=0) return;
    grid_->insertColumn(col-1);
    for (int r=0;r<grid_->rowCount();++r){
        auto *it = grid_->takeItem(r,col+1);
        grid_->setItem(r,col-1,it);
    }
    auto *hdr = grid_->takeHorizontalHeaderItem(col+1);
    grid_->setHorizontalHeaderItem(col-1, hdr);
    grid_->removeColumn(col+1);
    sqlPreview_->setText(buildSql());
}
void AccessQueryDesignerPage::onMoveRight(){
    auto items = grid_->selectedItems(); if (items.isEmpty()) return;
    int col = items.first()->column(); if (col>=grid_->columnCount()-1) return;
    grid_->insertColumn(col+2);
    for (int r=0;r<grid_->rowCount();++r){
        auto *it = grid_->takeItem(r,col);
        grid_->setItem(r,col+2,it);
    }
    auto *hdr = grid_->takeHorizontalHeaderItem(col);
    grid_->setHorizontalHeaderItem(col+2, hdr);
    grid_->removeColumn(col);
    sqlPreview_->setText(buildSql());
}
void AccessQueryDesignerPage::onClearGrid(){
    grid_->clear();
    grid_->setRowCount(2);
    grid_->setColumnCount(0);
    grid_->setVerticalHeaderLabels(QStringList() << "Criteria" << "Or");
    sqlPreview_->setText(buildSql());
}

void AccessQueryDesignerPage::insertIntoActiveCriteriaCell(const QString& text){
    auto items = grid_->selectedItems();
    int row = 0, col = 0;
    if (!items.isEmpty()){ row = items.first()->row(); col = items.first()->column(); }
    if (row<0 || row>1) row=0;
    if (col<0) return;
    auto *it = grid_->item(row,col);
    if (!it){ it = new QTableWidgetItem; grid_->setItem(row,col,it); }
    QString cur = it->text();
    if (!cur.isEmpty() && !cur.endsWith(' ')) cur += ' ';
    it->setText(cur + text);
    sqlPreview_->setText(buildSql());
}

void AccessQueryDesignerPage::onInsertOpEq(){ insertIntoActiveCriteriaCell("="); }
void AccessQueryDesignerPage::onInsertOpNe(){ insertIntoActiveCriteriaCell("<>"); }
void AccessQueryDesignerPage::onInsertOpGt(){ insertIntoActiveCriteriaCell(">"); }
void AccessQueryDesignerPage::onInsertOpLt(){ insertIntoActiveCriteriaCell("<"); }
void AccessQueryDesignerPage::onInsertOpGe(){ insertIntoActiveCriteriaCell(">="); }
void AccessQueryDesignerPage::onInsertOpLe(){ insertIntoActiveCriteriaCell("<="); }
void AccessQueryDesignerPage::onInsertOpLike(){ insertIntoActiveCriteriaCell("LIKE "); }
void AccessQueryDesignerPage::onInsertOpBetween(){ insertIntoActiveCriteriaCell("BETWEEN  AND "); }
void AccessQueryDesignerPage::onInsertOpIn(){ insertIntoActiveCriteriaCell("IN ()"); }
void AccessQueryDesignerPage::onInsertIsNull(){ insertIntoActiveCriteriaCell("IS NULL"); }
void AccessQueryDesignerPage::onInsertNotNull(){ insertIntoActiveCriteriaCell("IS NOT NULL"); }
void AccessQueryDesignerPage::onInsertTrue(){ insertIntoActiveCriteriaCell("TRUE"); }
void AccessQueryDesignerPage::onInsertFalse(){ insertIntoActiveCriteriaCell("FALSE"); }

QString AccessQueryDesignerPage::buildSql() const{
    const QString table = currentTable();
    if (table.isEmpty()) return "";

    // SELECT: si no hay columnas -> *
    QStringList cols;
    for (int c=0;c<grid_->columnCount();++c){
        auto *hdr = grid_->horizontalHeaderItem(c);
        if (!hdr) continue;
        const QString field = hdr->text().trimmed();
        if (field.isEmpty()) continue;
        cols << QString("%1.%2").arg(table, field);
    }
    const QString select = cols.isEmpty() ? "*" : cols.join(", ");

    // WHERE:
    auto rowExpr = [&](int row)->QString{
        QStringList ands;
        for (int c=0;c<grid_->columnCount();++c){
            auto *hdr = grid_->horizontalHeaderItem(c);
            if (!hdr) continue;
            const QString field = hdr->text().trimmed();
            if (field.isEmpty()) continue;
            auto *crit = grid_->item(row,c);
            if (!crit) continue;
            const QString cond = crit->text().trimmed();
            if (cond.isEmpty()) continue;
            ands << QString("%1.%2 %3").arg(table, field, cond);
        }
        return ands.join(" AND ");
    };
    const QString r0 = rowExpr(0), r1 = rowExpr(1);
    QString where;
    if(!r0.isEmpty() && !r1.isEmpty()) where = "(" + r0 + ") OR (" + r1 + ")";
    else if(!r0.isEmpty()) where = r0;
    else if(!r1.isEmpty()) where = r1;

    QString sql = "SELECT " + select + " FROM " + table;
    if(!where.isEmpty()) sql += " WHERE " + where;
    int lim = spLimit_->value(); if(lim>0) sql += " LIMIT " + QString::number(lim);
    return sql;
}

void AccessQueryDesignerPage::onRun(){
    const QString sql = buildSql();
    sqlPreview_->setText(sql);
    if (sql.trimmed().isEmpty()) { QMessageBox::information(this,"Ejecutar","La consulta está vacía."); return; }
    emit runSql(sql);
    status_->setText("Ejecutada.");
}

void AccessQueryDesignerPage::onSave(){
    QString name = edName_->text().trimmed();
    if (name.isEmpty()) { QMessageBox::warning(this,"Guardar","Ponle un nombre a la consulta."); return; }
    QString err;
    const QString sql = buildSql();
    if (!QueryStore::instance().save(name, sql, &err)) {
        QMessageBox::warning(this,"Guardar", err); return;
    }
    sqlPreview_->setText(sql);
    emit savedQuery(name);
    QMessageBox::information(this,"Guardar","Consulta guardada.");
}

void AccessQueryDesignerPage::onSaveAs(){
    bool ok=false;
    QString nn = QInputDialog::getText(this,"Guardar como","Nombre:", QLineEdit::Normal, edName_->text(), &ok).trimmed();
    if(!ok || nn.isEmpty()) return;
    edName_->setText(nn);
    onSave();
}

void AccessQueryDesignerPage::onRename(){
    QString oldName = edName_->text().trimmed(); if (oldName.isEmpty()) { QMessageBox::warning(this,"Renombrar","Primero escribe el nombre actual."); return; }
    bool ok=false;
    QString nn = QInputDialog::getText(this,"Renombrar","Nuevo nombre:", QLineEdit::Normal, oldName, &ok).trimmed();
    if(!ok || nn.isEmpty() || nn==oldName) return;
    QString err;
    if (!QueryStore::instance().rename(oldName, nn, &err)) { QMessageBox::warning(this,"Renombrar", err); return; }
    edName_->setText(nn);
    emit savedQuery(nn);
}

void AccessQueryDesignerPage::onDelete(){
    QString name = edName_->text().trimmed();
    if (name.isEmpty()) return;
    if (QMessageBox::question(this,"Eliminar","¿Eliminar la consulta?")==QMessageBox::No) return;
    QString err;
    if (!QueryStore::instance().remove(name, &err)) { QMessageBox::warning(this,"Eliminar", err); return; }
    emit savedQuery(name);
    QMessageBox::information(this,"Eliminar","Consulta eliminada.");
}
