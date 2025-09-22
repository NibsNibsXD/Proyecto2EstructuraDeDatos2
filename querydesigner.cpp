#include "querydesigner.h"
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
#include <QDate>
#include <QInputDialog>


// ===== helpers (idénticos a los de QueryPage) =====
int QueryDesignerPage::schemaFieldIndex(const Schema& s, const QString& name){
    for(int i=0;i<s.size();++i) if(s[i].name.compare(name, Qt::CaseInsensitive)==0) return i;
    return -1;
}
int QueryDesignerPage::cmpVar(const QVariant& a, const QVariant& b){
    if(!a.isValid() && !b.isValid()) return 0;
    if(!a.isValid()) return -1;
    if(!b.isValid()) return 1;
    if(a.userType()==QMetaType::QDate || b.userType()==QMetaType::QDate){
        QDate da = a.userType()==QMetaType::QDate ? a.toDate() : QDate::fromString(a.toString(),"yyyy-MM-dd");
        QDate db = b.userType()==QMetaType::QDate ? b.toDate() : QDate::fromString(b.toString(),"yyyy-MM-dd");
        if(!da.isValid() || !db.isValid())
            return QString::compare(a.toString(), b.toString(), Qt::CaseInsensitive);
        if(da<db) return -1; if(da>db) return 1; return 0;
    }
    bool okA=false, okB=false;
    double da=a.toDouble(&okA), db=b.toDouble(&okB);
    if(okA && okB){ if(da<db) return -1; if(da>db) return 1; return 0; }
    if(a.typeId()==QMetaType::LongLong || b.typeId()==QMetaType::LongLong){
        qlonglong ia=a.toLongLong(&okA), ib=b.toLongLong(&okB);
        if(okA && okB){ if(ia<ib) return -1; if(ia>ib) return 1; return 0; }
    }
    return QString::compare(a.toString(), b.toString(), Qt::CaseInsensitive);
}
bool QueryDesignerPage::equalVar(const QVariant& a, const QVariant& b){
    if(!a.isValid() && !b.isValid()) return true;
    return cmpVar(a,b)==0;
}

// ====================================================
QueryDesignerPage::QueryDesignerPage(QWidget* parent) : QWidget(parent){
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(8,8,8,8);
    root->setSpacing(6);

    // Fila superior: Tabla + Nombre de consulta y botones
    auto top = new QHBoxLayout; top->setSpacing(8);

    cbTable_ = new QComboBox;
    cbTable_->addItems(DataModel::instance().tables());

    edName_ = new QLineEdit; edName_->setPlaceholderText("Nombre de la consulta…");

    auto bSave = new QToolButton;   bSave->setText("Guardar");
    auto bRename = new QToolButton; bRename->setText("Renombrar");
    auto bDelete = new QToolButton; bDelete->setText("Eliminar");
    auto bRun = new QToolButton;    bRun->setText("Ejecutar");

    top->addWidget(new QLabel("Tabla:"));
    top->addWidget(cbTable_, 1);
    top->addSpacing(10);
    top->addWidget(new QLabel("Nombre:"));
    top->addWidget(edName_, 1);
    top->addSpacing(10);
    top->addWidget(bSave);
    top->addWidget(bRename);
    top->addWidget(bDelete);
    top->addSpacing(20);
    top->addWidget(bRun);

    // Centro: columnas + condiciones
    auto mid = new QHBoxLayout; mid->setSpacing(10);

    lwColumns_ = new QListWidget;
    lwColumns_->setSelectionMode(QAbstractItemView::NoSelection);
    lwColumns_->setMinimumWidth(180);

    auto condBox = new QVBoxLayout; condBox->setSpacing(6);
    twWhere_ = new QTableWidget(0,3);
    twWhere_->setHorizontalHeaderLabels(QStringList() << "Columna" << "Operador" << "Valor");
    twWhere_->horizontalHeader()->setStretchLastSection(true);
    twWhere_->verticalHeader()->setVisible(false);

    auto rowBtns = new QHBoxLayout;
    auto bAdd = new QToolButton; bAdd->setText("+ condición");
    auto bDel = new QToolButton; bDel->setText("− condición");
    rowBtns->addWidget(bAdd); rowBtns->addWidget(bDel); rowBtns->addStretch();

    auto orderRow = new QHBoxLayout;
    cbOrderBy_ = new QComboBox;
    btnDesc_ = new QToolButton; btnDesc_->setText("DESC"); btnDesc_->setCheckable(true);
    spLimit_ = new QSpinBox; spLimit_->setRange(0, 1000000); spLimit_->setPrefix("LIMIT "); spLimit_->setValue(0);
    orderRow->addWidget(new QLabel("Order by:")); orderRow->addWidget(cbOrderBy_);
    orderRow->addWidget(btnDesc_); orderRow->addSpacing(20);
    orderRow->addWidget(spLimit_); orderRow->addStretch();

    condBox->addWidget(twWhere_, 1);
    condBox->addLayout(rowBtns);
    condBox->addLayout(orderRow);

    mid->addWidget(lwColumns_, 0);
    mid->addLayout(condBox, 1);

    // Abajo: grid y status
    grid_ = new QTableWidget;
    grid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid_->horizontalHeader()->setStretchLastSection(true);

    status_ = new QLabel; status_->setStyleSheet("color:#444");

    root->addLayout(top);
    root->addLayout(mid, 1);
    root->addWidget(grid_, 1);
    root->addWidget(status_);

    // Señales
    connect(cbTable_, &QComboBox::currentTextChanged, this, &QueryDesignerPage::onTableChanged);
    connect(bAdd, &QToolButton::clicked, this, &QueryDesignerPage::onAddCond);
    connect(bDel, &QToolButton::clicked, this, &QueryDesignerPage::onDelCond);
    connect(bRun, &QToolButton::clicked, this, &QueryDesignerPage::onRun);
    connect(bSave, &QToolButton::clicked, this, &QueryDesignerPage::onSave);
    connect(bRename, &QToolButton::clicked, this, &QueryDesignerPage::onRename);
    connect(bDelete, &QToolButton::clicked, this, &QueryDesignerPage::onDelete);

    // Inicializar
    onTableChanged(cbTable_->currentText());
}

void QueryDesignerPage::rebuildColumns(){
    lwColumns_->clear();
    cbOrderBy_->clear();
    const Schema s = DataModel::instance().schema(cbTable_->currentText());
    for (const auto& f : s) {
        auto *it = new QListWidgetItem(f.name);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);            // por defecto seleccionado
        lwColumns_->addItem(it);
        cbOrderBy_->addItem(f.name);
    }
}

void QueryDesignerPage::onTableChanged(const QString&){
    rebuildColumns();
    // limpiar condiciones
    twWhere_->setRowCount(0);
}

void QueryDesignerPage::onAddCond(){
    const Schema s = DataModel::instance().schema(cbTable_->currentText());
    int r = twWhere_->rowCount(); twWhere_->insertRow(r);

    auto cbCol = new QComboBox; for (const auto& f : s) cbCol->addItem(f.name);
    auto cbOp  = new QComboBox; cbOp->addItems(QStringList() << "=" << "!=" << "<>" << ">" << ">=" << "<" << "<=");
    auto edVal = new QLineEdit;

    twWhere_->setCellWidget(r, 0, cbCol);
    twWhere_->setCellWidget(r, 1, cbOp);
    twWhere_->setCellWidget(r, 2, edVal);
}
void QueryDesignerPage::onDelCond(){
    int r = twWhere_->currentRow();
    if (r>=0) twWhere_->removeRow(r);
}

QString QueryDesignerPage::buildSql() const {
    // columnas
    QStringList cols;
    for (int i=0;i<lwColumns_->count();++i)
        if (lwColumns_->item(i)->checkState()==Qt::Checked)
            cols << lwColumns_->item(i)->text();
    if (cols.isEmpty()) cols << "*";

    QString sql = "SELECT " + cols.join(", ") +
                  " FROM " + cbTable_->currentText();

    // where
    QStringList whereParts;
    for (int r=0;r<twWhere_->rowCount();++r){
        auto cbCol = qobject_cast<QComboBox*>(twWhere_->cellWidget(r,0));
        auto cbOp  = qobject_cast<QComboBox*>(twWhere_->cellWidget(r,1));
        auto edVal = qobject_cast<QLineEdit*>(twWhere_->cellWidget(r,2));
        if (!cbCol || !cbOp || !edVal) continue;
        const QString col = cbCol->currentText();
        const QString op  = cbOp->currentText();
        QString val = edVal->text().trimmed();
        // si parece número/true/false/fecha, lo dejamos; sino, comillamos
        bool ok=false; (void)val.toDouble(&ok);
        const QString up = val.toUpper();
        const bool isBool = (up=="TRUE" || up=="FALSE");
        const bool isDate = QDate::fromString(val, "yyyy-MM-dd").isValid();
        if (!ok && !isBool && !isDate && !val.startsWith("'")) val = "'" + val + "'";
        whereParts << (col + " " + op + " " + val);
    }
    if (!whereParts.isEmpty()) sql += " WHERE " + whereParts.join(" AND ");

    // order/limit
    if (cbOrderBy_->count()>0) {
        sql += " ORDER BY " + cbOrderBy_->currentText();
        if (btnDesc_->isChecked()) sql += " DESC";
    }
    if (spLimit_->value() > 0) sql += " LIMIT " + QString::number(spLimit_->value());
    sql += ";";
    return sql;
}

void QueryDesignerPage::execSelectSql(const QString& sql){
    // Implementación local: ejecutar como en QueryPage::execSelect
    auto& dm = DataModel::instance();
    QString s = sql.trimmed();
    if (s.endsWith(';')) s.chop(1);

    // parseo mínimo: SELECT col FROM tabla ...
    int pFrom = s.indexOf(" FROM ", 0, Qt::CaseInsensitive);
    if (pFrom<0) { QMessageBox::warning(this, "SELECT", "SQL inválido"); return; }

    QString colsPart = s.mid(7, pFrom-7).trimmed();
    QString rest = s.mid(pFrom+6).trimmed();

    QString table = rest;
    int pWS = table.indexOf(' ');
    if (pWS>0) table = table.left(pWS);
    table = table.trimmed();
    if (table.endsWith(';')) table.chop(1);
    const Schema schema = dm.schema(table);
    if (schema.isEmpty()) { QMessageBox::warning(this, "SELECT", "Tabla no encontrada"); return; }

    // columnas
    QStringList cols;
    if (colsPart=="*") for (const auto& f : schema) cols<<f.name;
    else for (QString c : colsPart.split(',', Qt::SkipEmptyParts)) cols << c.trimmed();

    // preparar header
    grid_->clear(); grid_->setRowCount(0);
    grid_->setColumnCount(cols.size());
    grid_->setHorizontalHeaderLabels(cols);

    // recolectar filas (sin WHERE aquí; ya filtramos con buildSql → lo metimos literal)
    const auto& data = dm.rows(table);
    int r=0; grid_->setRowCount(data.size());
    for (int i=0;i<data.size();++i){
        const auto& rec = data[i];
        if (rec.isEmpty()) continue;
        for (int c=0;c<cols.size();++c){
            int ix = schemaFieldIndex(schema, cols[c]);
            if (ix<0) { QMessageBox::warning(this,"SELECT","Columna inválida"); return; }
            auto *it = new QTableWidgetItem(rec.value(ix).toString());
            grid_->setItem(r,c,it);
        }
        ++r;
    }
    grid_->setRowCount(r);
    status_->setText(QString::number(r) + " fila(s) — " + sql);
}

void QueryDesignerPage::onRun(){
    execSelectSql(buildSql());
}

void QueryDesignerPage::onSave(){
    QString name = edName_->text().trimmed();
    if (name.isEmpty()) { QMessageBox::warning(this,"Guardar","Ponle un nombre a la consulta."); return; }
    QString err;
    if (!QueryStore::instance().save(name, buildSql(), &err)) {
        QMessageBox::warning(this,"Guardar", err);
        return;
    }
    emit savedQuery(name);
    QMessageBox::information(this, "Guardar", "Consulta guardada.");
}

void QueryDesignerPage::onRename(){
    QString oldName = edName_->text().trimmed();
    if (oldName.isEmpty()) { QMessageBox::warning(this,"Renombrar","Primero escribe el nombre actual."); return; }
    bool ok=false;
    QString nn = QInputDialog::getText(this,"Renombrar","Nuevo nombre:", QLineEdit::Normal, oldName, &ok);
    if(!ok || nn.trimmed().isEmpty()) return;
    QString err;
    if (!QueryStore::instance().rename(oldName, nn.trimmed(), &err)) {
        QMessageBox::warning(this,"Renombrar", err); return;
    }
    edName_->setText(nn.trimmed());
    emit savedQuery(nn.trimmed());
}

void QueryDesignerPage::onDelete(){
    QString name = edName_->text().trimmed();
    if (name.isEmpty()) return;
    QString err;
    if (!QueryStore::instance().remove(name, &err)) {
        QMessageBox::warning(this,"Eliminar", err); return;
    }
    emit savedQuery(name);
    QMessageBox::information(this, "Eliminar", "Consulta eliminada.");
}
