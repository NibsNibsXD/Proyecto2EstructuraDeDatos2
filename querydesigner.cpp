#include "querydesigner.h"
// ya no dependemos de QueryStore para guardar en UI; usamos DataModel
#include "datamodel.h"

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

// ---------- normalización de tokens de columna ----------
static QString normalizeColToken(QString t){
    t = t.trimmed();
    if ((t.startsWith('[') && t.endsWith(']')) ||
        (t.startsWith('"') && t.endsWith('"')) ||
        (t.startsWith('`') && t.endsWith('`'))) {
        t = t.mid(1, t.size()-2);
    }
    int dot = t.lastIndexOf('.');
    if (dot > 0) t = t.mid(dot+1).trimmed();
    return t;
}

// ===== helpers (mismos criterios que QueryPage) =====
int QueryDesignerPage::schemaFieldIndex(const Schema& s, const QString& name){
    const QString n = normalizeColToken(name);
    for(int i=0;i<s.size();++i) if(s[i].name.compare(n, Qt::CaseInsensitive)==0) return i;
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

static QString up(const QString& s){ return QString(s).toUpper(); }
static QVariant parseLiteral(const QString& tok){
    QString s=tok.trimmed();
    if(s.startsWith('\'') && s.endsWith('\'')) return s.mid(1,s.size()-2);
    if(up(s)=="TRUE") return true;
    if(up(s)=="FALSE") return false;
    if(up(s)=="NULL") return QVariant();
    bool ok=false;
    qlonglong i=s.toLongLong(&ok); if(ok) return QVariant::fromValue(i);
    double d=s.toDouble(&ok); if(ok) return d;
    QDate dt=QDate::fromString(s, "yyyy-MM-dd"); if(dt.isValid()) return dt;
    return s;
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

    // Abajo: grid y status (asegurar visibilidad)
    grid_ = new QTableWidget;
    grid_->setObjectName("designerGrid");
    grid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid_->horizontalHeader()->setStretchLastSection(true);
    grid_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    grid_->setMinimumHeight(180);

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
        const QString upv = val.toUpper();
        const bool isBool = (upv=="TRUE" || upv=="FALSE");
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
    // Ejecutar SELECT con WHERE/ORDER/LIMIT localmente (como QueryPage)
    auto& dm = DataModel::instance();
    QString s = sql.trimmed();
    if (s.endsWith(';')) s.chop(1);

    // parseo mínimo: SELECT col FROM tabla ...
    int pFrom = s.indexOf(" FROM ", 0, Qt::CaseInsensitive);
    if (pFrom<0) { QMessageBox::warning(this, "SELECT", "SQL inválido"); return; }

    QString colsPart = s.mid(7, pFrom-7).trimmed();
    QString rest = s.mid(pFrom+6).trimmed();

    // tabla
    QString table = rest;
    int pWS = table.indexOf(' ');
    if (pWS>0) table = table.left(pWS);
    table = table.trimmed();
    if (table.endsWith(';')) table.chop(1);

    const Schema schema = dm.schema(table);
    if (schema.isEmpty()) { QMessageBox::warning(this, "SELECT", "Tabla no encontrada"); return; }
    const auto& data = dm.rows(table);

    // columnas
    QStringList cols, lookups;
    if (colsPart=="*") {
        for (const auto& f : schema) { cols<<f.name; lookups<<f.name; }
    } else {
        for (QString c : colsPart.split(',', Qt::SkipEmptyParts)){
            c = c.trimmed();
            cols    << c;
            lookups << normalizeColToken(c);
        }
    }

    // WHERE
    struct Cond { QString col; QString op; QVariant val; };
    QVector<Cond> where;
    int pWhere = rest.indexOf(" WHERE ", Qt::CaseInsensitive);
    int pOrder = rest.indexOf(" ORDER BY ", Qt::CaseInsensitive);
    int pLimit = rest.indexOf(" LIMIT ", Qt::CaseInsensitive);
    int endRest = rest.size();
    if (pOrder>=0) endRest = qMin(endRest, pOrder);
    if (pLimit>=0) endRest = qMin(endRest, pLimit);

    if (pWhere==0) {
        QString wherePart = rest.mid(7, endRest-7).trimmed();
        auto parts = wherePart.split(QRegularExpression("\\bAND\\b"), Qt::SkipEmptyParts);
        QRegularExpression re("^([A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)?)\\s*(=|!=|<>|>=|<=|>|<)\\s*(.+)$");
        for (const QString& w : parts) {
            auto m = re.match(w.trimmed());
            if (!m.hasMatch()) { QMessageBox::warning(this, "SELECT", "WHERE inválido"); return; }
            where.push_back({ normalizeColToken(m.captured(1)), m.captured(2), parseLiteral(m.captured(3).trimmed()) });
        }
    }

    // ORDER BY
    QString orderCol; bool orderDesc=false;
    if (pOrder>=0) {
        QString tail = rest.mid(pOrder+10).trimmed(); // después de "ORDER BY "
        QStringList t = tail.split(' ', Qt::SkipEmptyParts);
        if (!t.isEmpty()) orderCol = t[0];
        if (t.size()>1 && up(t[1])=="DESC") orderDesc = true;
    }

    // LIMIT
    int limit = -1;
    if (pLimit>=0) {
        QString n = rest.mid(pLimit+7).trimmed(); // después de "LIMIT "
        bool ok=false; int v = n.toInt(&ok);
        if (ok) limit = v; else { QMessageBox::warning(this, "SELECT", "LIMIT inválido"); return; }
    }

    // preparar header
    grid_->setVisible(true);
    grid_->clear(); grid_->setRowCount(0);
    grid_->setColumnCount(cols.size());
    grid_->setHorizontalHeaderLabels(cols);

    // predicado
    auto matchRow = [&](const Record& r)->bool{
        for (const auto& c : where) {
            int ci = schemaFieldIndex(schema, c.col); if (ci<0) return false;
            QVariant v = r.value(ci);
            QString op = c.op; if (op=="<>") op = "!=";

            if(op=="=")  { if(!equalVar(v, c.val)) return false; continue; }
            if(op=="!=") { if( equalVar(v, c.val)) return false; continue; }

            int cmp = cmpVar(v, c.val);
            if(op==">"  && !(cmp>0))  return false;
            if(op=="<"  && !(cmp<0))  return false;
            if(op==">=" && !(cmp>=0)) return false;
            if(op=="<=" && !(cmp<=0)) return false;
        }
        return true;
    };

    // recolectar
    struct Row { const Record* rec; };
    QVector<Row> rows; rows.reserve(data.size());
    for (const auto& r : data) {
        if (r.isEmpty()) continue;
        if (matchRow(r)) rows.push_back({ &r });
    }

    // ordenar
    if (!orderCol.isEmpty()) {
        int oi = schemaFieldIndex(schema, orderCol);
        if (oi<0) { QMessageBox::warning(this, "SELECT", "ORDER BY columna inválida"); return; }
        std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b){
            int cmp = cmpVar(a.rec->value(oi), b.rec->value(oi));
            return orderDesc ? (cmp>0) : (cmp<0);
        });
    }

    // LIMIT
    int take = rows.size(); if (limit>=0) take = qMin(take, limit);

    // volcado
    grid_->setRowCount(take);
    for (int r=0; r<take; ++r) {
        for (int c=0; c<cols.size(); ++c) {
            int ix = schemaFieldIndex(schema, lookups[c]);
            if (ix<0) { QMessageBox::warning(this, "SELECT", QString("Columna '%1' no existe").arg(cols[c])); return; }
            auto *it = new QTableWidgetItem(rows[r].rec->value(ix).toString());
            grid_->setItem(r, c, it);
        }
    }
    status_->setText(QString::number(take) + " fila(s) — " + sql);
}

void QueryDesignerPage::onRun(){
    execSelectSql(buildSql());
    grid_->setFocus();
}

void QueryDesignerPage::onSave(){
    const QString name = edName_->text().trimmed();
    if (name.isEmpty()) { QMessageBox::warning(this,"Guardar","Ponle un nombre a la consulta."); return; }
    QString err;
    if (!DataModel::instance().saveQuery(name, buildSql(), &err)) {
        QMessageBox::warning(this,"Guardar", err.isEmpty()? "No se pudo guardar." : err);
        return;
    }
    // DataModel emite queriesChanged → ShellWindow refresca la lista
    QMessageBox::information(this, "Guardar", "Consulta guardada.");
}

void QueryDesignerPage::onRename(){
    const QString oldName = edName_->text().trimmed();
    if (oldName.isEmpty()) {
        QMessageBox::warning(this,"Renombrar","Primero escribe el nombre actual.");
        return;
    }

    bool ok=false;
    const QString nn = QInputDialog::getText(
                           this,"Renombrar","Nuevo nombre:", QLineEdit::Normal, oldName, &ok
                           ).trimmed();
    if(!ok || nn.isEmpty()) return;

    const QString newName = nn;
    auto& dm = DataModel::instance();

    // Si solo cambia casing, no hacemos nada especial
    if (QString::compare(oldName, newName, Qt::CaseInsensitive) == 0) {
        edName_->setText(newName);
        QMessageBox::information(this, "Renombrar", "Sin cambios.");
        return;
    }

    // Confirmar sobreescritura si existe
    bool exists = false;
    for (const auto& n : dm.queries())
        if (QString::compare(n, newName, Qt::CaseInsensitive) == 0) { exists = true; break; }
    if (exists) {
        auto ret = QMessageBox::question(this,"Renombrar",
                                         QString("“%1” ya existe. ¿Deseas reemplazarla?").arg(newName),
                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    // SQL actual de la consulta vieja
    const QString sql = dm.querySql(oldName);
    if (sql.isEmpty()) {
        QMessageBox::warning(this, "Renombrar", "No se encontró SQL de la consulta original.");
        return;
    }

    // Guardar como nuevo y eliminar la vieja
    QString err;
    if (!dm.saveQuery(newName, sql, &err)) {
        QMessageBox::warning(this,"Renombrar", err.isEmpty()? "No se pudo guardar con el nuevo nombre." : err);
        return;
    }
    QString err2;
    if (QString::compare(oldName, newName, Qt::CaseInsensitive) != 0)
        dm.removeQuery(oldName, &err2);

    edName_->setText(newName);
    QMessageBox::information(this, "Renombrar", "Consulta renombrada.");
}

void QueryDesignerPage::onDelete(){
    const QString name = edName_->text().trimmed();
    if (name.isEmpty()) return;
    QString err;
    if (!DataModel::instance().removeQuery(name, &err)) {
        QMessageBox::warning(this,"Eliminar", err.isEmpty()? "No se pudo eliminar." : err);
        return;
    }
    QMessageBox::information(this, "Eliminar", "Consulta eliminada.");
}
