#include "querypage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QHeaderView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QComboBox>
#include <QTableWidget>
#include <QLabel>
#include <QKeyEvent>
#include <QToolBar>
#include <QAction>
#include <QInputDialog>
#include <algorithm>

static bool isTomb(const Record& r){ return r.isEmpty(); }

// ---------- normalización de tokens de columna ----------
static QString normalizeColToken(QString t){
    t = t.trimmed();
    // quita envoltorios: [Col], "Col", `Col`
    if ((t.startsWith('[') && t.endsWith(']')) ||
        (t.startsWith('"') && t.endsWith('"')) ||
        (t.startsWith('`') && t.endsWith('`'))) {
        t = t.mid(1, t.size()-2);
    }
    // si viene calificado Tabla.Columna, quedarnos con la parte derecha
    int dot = t.lastIndexOf('.');
    if (dot > 0) t = t.mid(dot+1).trimmed();
    return t;
}

// -------- helpers locales (evitan usar métodos privados de DataModel) --------
static int schemaFieldIndex(const Schema& s, const QString& name){
    const QString n = normalizeColToken(name);
    for (int i = 0; i < s.size(); ++i)
        if (s[i].name.compare(n, Qt::CaseInsensitive) == 0) return i;
    return -1;
}
static int cmpVar(const QVariant& a, const QVariant& b){
    // Nulls
    if(!a.isValid() && !b.isValid()) return 0;
    if(!a.isValid()) return -1;
    if(!b.isValid()) return 1;

    // Fecha
    if(a.userType()==QMetaType::QDate || b.userType()==QMetaType::QDate){
        QDate da = (a.userType()==QMetaType::QDate)? a.toDate() : QDate::fromString(a.toString(), "yyyy-MM-dd");
        QDate db = (b.userType()==QMetaType::QDate)? b.toDate() : QDate::fromString(b.toString(), "yyyy-MM-dd");
        if(!da.isValid() || !db.isValid()){
            // fallback string si alguna no parsea
            return QString::compare(a.toString(), b.toString(), Qt::CaseInsensitive);
        }
        if(da<db) return -1; if(da>db) return 1; return 0;
    }

    // Numérico
    bool okA=false, okB=false;
    double da=a.toDouble(&okA), db=b.toDouble(&okB);
    if(okA && okB){
        if(da<db) return -1; if(da>db) return 1; return 0;
    }

    // Enteros largos
    if(a.typeId()==QMetaType::LongLong || b.typeId()==QMetaType::LongLong){
        qlonglong ia=a.toLongLong(&okA), ib=b.toLongLong(&okB);
        if(okA && okB){ if(ia<ib) return -1; if(ia>ib) return 1; return 0; }
    }

    // Por defecto: string case-insensitive
    return QString::compare(a.toString(), b.toString(), Qt::CaseInsensitive);
}
static bool equalVar(const QVariant& a, const QVariant& b){
    if(!a.isValid() && !b.isValid()) return true;
    return cmpVar(a,b)==0;
}

// -------- util de parsing --------
static QString takeAfter(const QString& src, const QString& kw){
    int i = src.indexOf(kw, 0, Qt::CaseInsensitive);
    if(i<0) return {};
    return src.mid(i+kw.size()).trimmed();
}
static QList<QString> tokenize(const QString& s){
    QList<QString> out; QString cur; bool inStr=false;
    for(int i=0;i<s.size();++i){
        QChar c=s[i];
        if(inStr){
            cur+=c;
            if(c=='\''){ out<<cur; cur.clear(); inStr=false; }
        } else if(c=='\''){
            if(!cur.isEmpty()){ out<<cur; cur.clear(); }
            cur="\'"; inStr=true;
        } else if(QString("(),*=<>!+-/").contains(c) || c.isSpace()){
            if(!cur.isEmpty()){ out<<cur; cur.clear(); }
            if(!c.isSpace()) out<<QString(c);
        } else cur+=c;
    }
    if(!cur.isEmpty()) out<<cur;
    return out;
}

// -------- resolver nombre real de tabla ignorando mayúsc/minúsc --------
static QString resolveTableNameCI(const QString& raw) {
    auto &dm = DataModel::instance();
    for (const QString& t : dm.tables()) {
        if (QString::compare(t, raw, Qt::CaseInsensitive) == 0)
            return t;
    }
    return QString();
}

// ===================== QueryPage =====================
QueryPage::QueryPage(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10,10,10,10);
    root->setSpacing(8);

    // ===== Toolbar (Guardar) =====
    toolbar_ = new QToolBar(this);
    actSave_   = toolbar_->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Guardar"));
    actSaveAs_ = toolbar_->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Guardar como…"));
    actSave_->setShortcut(QKeySequence::Save);              // Ctrl+S
    actSaveAs_->setShortcut(QKeySequence("Ctrl+Shift+S"));  // Ctrl+Shift+S
    connect(actSave_,   &QAction::triggered, this, &QueryPage::save);
    connect(actSaveAs_, &QAction::triggered, this, &QueryPage::saveAs);
    root->addWidget(toolbar_);

    // ===== Editor + acciones rápidas =====
    auto* top = new QWidget;
    auto* th = new QHBoxLayout(top);
    th->setContentsMargins(0,0,0,0);

    m_sql = new QPlainTextEdit;
    m_sql->setPlaceholderText(
        "-- SQL minimal soportado\n"
        "SELECT *|col1,col2 FROM tabla [WHERE col op valor [AND col op valor ...]] [ORDER BY col [DESC]] [LIMIT n];\n"
        "INSERT INTO tabla (col1,col2,...) VALUES (v1,v2,...);\n"
        "DELETE FROM tabla [WHERE col op valor];\n"
        "(Ctrl+Enter para ejecutar · Ctrl+S para guardar)"
        );
    m_sql->setFixedHeight(120);

    auto* run = new QToolButton; run->setText("Run"); run->setToolTip("Ejecutar (Ctrl+Enter)");
    auto* clear = new QToolButton; clear->setText("Limpiar");
    m_examples = new QComboBox; m_examples->addItem("Ejemplos...");
    m_examples->addItem("SELECT * FROM Tabla;");
    m_examples->addItem("SELECT id,nombre FROM Tabla WHERE id >= 10 ORDER BY nombre DESC LIMIT 50;");
    m_examples->addItem("INSERT INTO Tabla (nombre,activo) VALUES ('Alice', true);");
    m_examples->addItem("DELETE FROM Tabla WHERE id = 7;");

    th->addWidget(m_sql, 1);
    auto* side = new QVBoxLayout; side->setSpacing(6);
    side->addWidget(run);
    side->addWidget(clear);
    side->addWidget(m_examples);
    side->addStretch();
    th->addLayout(side);
    top->setLayout(th);

    m_grid = new QTableWidget;
    m_grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_grid->horizontalHeader()->setStretchLastSection(true);

    m_status = new QLabel; m_status->setStyleSheet("color:#444");

    root->addWidget(top);
    root->addWidget(m_grid, 1);
    root->addWidget(m_status);

    connect(run, &QToolButton::clicked, this, &QueryPage::runQuery);
    connect(clear, &QToolButton::clicked, this, &QueryPage::clearEditor);
    connect(m_examples, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QueryPage::loadExample);

    // atajo Ctrl+Enter (Ctrl+Return ejecuta) y Ctrl+S / Ctrl+Shift+S (ya con acciones)
    m_sql->installEventFilter(this);
}

bool QueryPage::eventFilter(QObject* obj, QEvent* ev){
    if(obj==m_sql && ev->type()==QEvent::KeyPress){
        auto* ke = static_cast<QKeyEvent*>(ev);
        // Ejecutar
        if((ke->modifiers() & Qt::ControlModifier) && (ke->key()==Qt::Key_Return || ke->key()==Qt::Key_Enter)){
            runQuery();
            return true;
        }
        // Guardar (además de los atajos de QAction, por si el foco está en el editor)
        if ((ke->modifiers() & Qt::ControlModifier) && ke->key()==Qt::Key_S) {
            if (ke->modifiers() & Qt::ShiftModifier) saveAs();
            else save();
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void QueryPage::clearEditor(){
    m_sql->clear();
    m_status->clear();
    m_grid->setRowCount(0);
    m_grid->setColumnCount(0);
    currentSql_.clear();
}

void QueryPage::loadExample(int idx){
    if(idx<=0) return;
    m_sql->setPlainText(m_examples->itemText(idx));
}

QString QueryPage::normWS(const QString& s){
    return s.trimmed().replace("\n"," ").replace(QRegularExpression("\\s+"), " ");
}
QString QueryPage::up(const QString& s){ return QString(s).toUpper(); }

QStringList QueryPage::splitCsv(const QString& s){
    QString t=s.trimmed();
    if(t.startsWith('(') && t.endsWith(')')) t=t.mid(1,t.size()-2);
    QString cur; bool inStr=false; QStringList out;
    for(int i=0;i<t.size();++i){
        QChar c=t[i];
        if(c=='\''){ inStr=!inStr; cur+=c; }
        else if(c==',' && !inStr){ out<<cur.trimmed(); cur.clear(); }
        else cur+=c;
    }
    if(!cur.trimmed().isEmpty()) out<<cur.trimmed();
    return out;
}

QVariant QueryPage::parseLiteral(const QString& tok){
    QString s=tok.trimmed();
    if(s.startsWith('\'') && s.endsWith('\'')) return s.mid(1,s.size()-2);
    if(up(s)=="TRUE") return true;
    if(up(s)=="FALSE") return false;
    if(up(s)=="NULL") return QVariant();
    bool ok=false;
    qlonglong i=s.toLongLong(&ok); if(ok) return QVariant::fromValue(i);
    double d=s.toDouble(&ok); if(ok) return d;
    QDate dt=QDate::fromString(s, "yyyy-MM-dd"); if(dt.isValid()) return dt;
    return s; // fallback string
}

bool QueryPage::parseSelect(const QString& sql, SelectSpec& out, QString* err){
    QString s=normWS(sql);
    if(!s.startsWith("SELECT ", Qt::CaseInsensitive)) return false;

    int pFrom = s.indexOf(" FROM ", 0, Qt::CaseInsensitive);
    if(pFrom<0){ if(err) *err="Falta FROM"; return false; }

    QString colsPart = s.mid(7, pFrom-7).trimmed();
    QString rest = s.mid(pFrom+6).trimmed();

    // ---- tabla (recorta ; si viene pegado) ----
    QString table = rest;
    int pWS = table.indexOf(' ');
    if(pWS>0) table = table.left(pWS);
    table = table.trimmed();
    if(table.endsWith(';')) table.chop(1);
    out.table = table;
    rest = rest.mid(table.size()).trimmed();

    // columnas
    if(colsPart=="*") out.columns.clear();
    else { out.columns = colsPart.split(',', Qt::SkipEmptyParts); for(QString& c: out.columns) c=c.trimmed(); }

    // WHERE
    if(rest.startsWith("WHERE ", Qt::CaseInsensitive)){
        int pAfterWhere = 6;
        int pOrder = rest.indexOf(" ORDER BY ", Qt::CaseInsensitive);
        int pLimit = rest.indexOf(" LIMIT ", Qt::CaseInsensitive);
        int end = rest.size(); if(pOrder>=0) end = qMin(end, pOrder); if(pLimit>=0) end=qMin(end,pLimit);
        QString where = rest.mid(pAfterWhere, end-pAfterWhere).trimmed();
        auto parts = where.split(QRegularExpression("\\bAND\\b"), Qt::SkipEmptyParts);

        // permite Tabla.Columna
        QRegularExpression re("^([A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)?)\\s*(=|!=|<>|>=|<=|>|<)\\s*(.+)$");
        for(const QString& w : parts){
            auto m=re.match(w.trimmed());
            if(!m.hasMatch()){ if(err) *err="WHERE inválido"; return false; }
            SelectSpec::Cond c{ normalizeColToken(m.captured(1)), m.captured(2), parseLiteral(m.captured(3).trimmed()) };
            out.where<<c;
        }
        rest = rest.mid(end).trimmed();
    }

    // ORDER BY
    if(rest.startsWith("ORDER BY ", Qt::CaseInsensitive)){
        QString tail = takeAfter(rest, "ORDER BY ");
        QStringList t = tail.split(' ', Qt::SkipEmptyParts);
        out.orderBy = t.value(0);                    // puede venir calificado; schemaFieldIndex lo normaliza
        out.orderDesc = (t.size()>1 && up(t[1])=="DESC");
        // recorta aproximado
        int pos = rest.indexOf(out.orderBy, 0, Qt::CaseInsensitive);
        rest = rest.mid(pos + out.orderBy.size());
        if(rest.trimmed().toUpper().startsWith(" DESC")) rest = rest.trimmed().mid(5);
        rest = rest.trimmed();
    }

    // LIMIT
    if(rest.startsWith("LIMIT ", Qt::CaseInsensitive)){
        QString n = takeAfter(rest, "LIMIT "); bool ok=false; out.limit = n.toInt(&ok);
        if(!ok){ if(err) *err="LIMIT inválido"; return false; }
    }
    return true;
}

bool QueryPage::parseInsert(const QString& sql, InsertSpec& out, QString* err){
    QString s=normWS(sql);
    QRegularExpression re("^INSERT\\s+INTO\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(([^)]*)\\)\\s*VALUES\\s*\\(([^)]*)\\)\\s*;?$");
    auto m=re.match(s, QRegularExpression::CaseInsensitiveOption);
    if(!m.hasMatch()) return false;
    out.table = m.captured(1);
    out.cols = splitCsv(m.captured(2));
    auto vals = splitCsv(m.captured(3));
    if(out.cols.size()!=vals.size()){ if(err) *err="# de columnas no coincide con # de valores"; return false; }
    for(const QString& v: vals) out.vals<<parseLiteral(v);
    return true;
}

bool QueryPage::parseDelete(const QString& sql, DeleteSpec& out, QString* err){
    QString s=normWS(sql);
    QRegularExpression re("^DELETE\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*(WHERE\\s+(.+))?;?$");
    auto m=re.match(s, QRegularExpression::CaseInsensitiveOption);
    if(!m.hasMatch()) return false;
    out.table=m.captured(1);
    if(m.captured(3).isEmpty()){ out.hasWhere=false; return true; }

    // permite Tabla.Columna en DELETE
    QRegularExpression rc("^([A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)?)\\s*(=|!=|<>|>=|<=|>|<)\\s*(.+)$");
    auto mw = rc.match(m.captured(3).trimmed());
    if(!mw.hasMatch()){ if(err) *err="WHERE inválido"; return false; }
    out.hasWhere=true; out.where={ normalizeColToken(mw.captured(1)), mw.captured(2), parseLiteral(mw.captured(3).trimmed()) };
    return true;
}

void QueryPage::runQuery(){
    QString sql = m_sql->toPlainText().trimmed();
    if(sql.isEmpty()) return;

    // ⬇️ quita ; final (si lo hay) para no contaminar WHERE/valores
    if (sql.endsWith(';'))
        sql.chop(1);

    QString err;
    SelectSpec qs; InsertSpec qi; DeleteSpec qd;
    if(parseSelect(sql, qs, &err)) { execSelect(qs); return; }
    if(parseInsert(sql, qi, &err)) { execInsert(qi); return; }
    if(parseDelete(sql, qd, &err)) { execDelete(qd); return; }
    QMessageBox::warning(this, "SQL", QString("No se pudo interpretar la consulta. %1").arg(err));
}

void QueryPage::execSelect(const SelectSpec& q){
    auto& dm = DataModel::instance();
    const QString table = resolveTableNameCI(q.table);
    if(table.isEmpty()){ QMessageBox::warning(this, "SELECT", "Tabla no encontrada"); return; }

    const auto s = dm.schema(table);

    // columnas (mantén encabezados como el usuario los escribió, usa normalizados para buscar)
    QStringList cols = q.columns;
    QStringList lookups;
    if(cols.isEmpty()){
        for(const auto& f : s){ cols<<f.name; lookups<<f.name; }
    } else {
        for(const QString& c : cols) lookups << normalizeColToken(c);
    }

    // map de nombre->índice
    QVector<int> colIdx; colIdx.reserve(cols.size());
    for(int i=0;i<cols.size();++i){
        int ix = schemaFieldIndex(s, lookups[i]);
        if(ix<0){ QMessageBox::warning(this, "SELECT", QString("Columna '%1' no existe").arg(cols[i])); return; }
        colIdx<<ix;
    }

    // header grid
    m_grid->clear();
    m_grid->setRowCount(0);
    m_grid->setColumnCount(cols.size());
    m_grid->setHorizontalHeaderLabels(cols);

    // filtro
    auto matchRow = [&](const Record& r){
        for(const auto& cond : q.where){
            int ci = schemaFieldIndex(s, cond.col); if(ci<0) return false;
            const QVariant v=r.value(ci);
            QString op=cond.op; if(op=="<>") op="!=";

            if(op=="=")  { if(!equalVar(v, cond.val)) return false; continue; }
            if(op=="!=") { if( equalVar(v, cond.val)) return false; continue; }

            int c = cmpVar(v, cond.val);
            if(op==">"  && !(c>0))  return false;
            if(op=="<"  && !(c<0))  return false;
            if(op==">=" && !(c>=0)) return false;
            if(op=="<=" && !(c<=0)) return false;
        }
        return true;
    };

    // recolecta
    struct Row { int idx; const Record* rec; };
    QVector<Row> rows;
    const auto& data = dm.rows(table);
    rows.reserve(data.size());
    for(int i=0;i<data.size();++i){
        const auto& r=data[i]; if(isTomb(r)) continue;
        if(matchRow(r)) rows.push_back({i,&r});
    }

    // ordenar
    if(!q.orderBy.isEmpty()){
        int oi = schemaFieldIndex(s, q.orderBy); // soporta calificado
        if(oi<0){ QMessageBox::warning(this, "SELECT", "ORDER BY columna inválida"); return; }
        std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b){
            int cmp = cmpVar(a.rec->value(oi), b.rec->value(oi));
            return q.orderDesc ? (cmp>0) : (cmp<0);
        });
    }

    // LIMIT
    int take = rows.size(); if(q.limit>=0) take = qMin(take, q.limit);

    // llenar
    m_grid->setRowCount(take);
    for(int r=0;r<take;++r){
        const auto& rec = *rows[r].rec;
        for(int c=0;c<colIdx.size();++c){
            auto it = new QTableWidgetItem;
            it->setText(rec.value(colIdx[c]).toString());
            m_grid->setItem(r,c,it);
        }
    }

    m_status->setText(QString("%1 fila(s)").arg(take));
}

void QueryPage::execInsert(const InsertSpec& q){
    auto& dm = DataModel::instance();
    const QString table = resolveTableNameCI(q.table);
    if(table.isEmpty()){ QMessageBox::warning(this, "INSERT", "Tabla no encontrada"); return; }

    const auto s = dm.schema(table);

    Record rec; rec.resize(s.size());
    for(int i=0;i<rec.size();++i) rec[i]=QVariant(); // NULLs por defecto

    for(int i=0;i<q.cols.size();++i){
        const QString col=q.cols[i];
        int ix=schemaFieldIndex(s,col);
        if(ix<0){ QMessageBox::warning(this, "INSERT", QString("Columna '%1' no existe").arg(col)); return; }
        rec[ix]=q.vals[i];
    }

    QString err;
    if(!dm.insertRow(table, rec, &err)){
        QMessageBox::warning(this, "INSERT", err);
        return;
    }
    m_status->setText("1 fila insertada");
}

void QueryPage::setSqlText(const QString& sql){
    m_sql->setPlainText(sql);
    m_sql->setFocus();
    currentSql_ = sql;
}

void QueryPage::execDelete(const DeleteSpec& q){
    auto& dm = DataModel::instance();
    const QString table = resolveTableNameCI(q.table);
    if(table.isEmpty()){ QMessageBox::warning(this, "DELETE", "Tabla no encontrada"); return; }

    const auto s = dm.schema(table);

    QList<int> toDel;
    const auto& data = dm.rows(table);

    auto match = [&](const Record& r){
        if(!q.hasWhere) return true;
        int ci = schemaFieldIndex(s, q.where.col); if(ci<0) return false;
        const QVariant v=r.value(ci);
        QString op=q.where.op; if(op=="<>") op="!=";

        if(op=="=")  return equalVar(v, q.where.val);
        if(op=="!=") return !equalVar(v, q.where.val);

        int c = cmpVar(v, q.where.val);
        if(op==">")  return c>0;
        if(op=="<")  return c<0;
        if(op==">=") return c>=0;
        if(op=="<=") return c<=0;
        return false;
    };

    for(int i=0;i<data.size();++i){
        const auto& r=data[i]; if(isTomb(r)) continue;
        if(match(r)) toDel<<i;
    }
    if(toDel.isEmpty()){ m_status->setText("0 filas borradas"); return; }

    QString err;
    if(!dm.removeRows(table, toDel, &err)){
        QMessageBox::warning(this, "DELETE", err);
        return;
    }
    m_status->setText(QString::number(toDel.size())+" fila(s) borradas");
}

// ===================== Guardado / Carga =====================

QString QueryPage::collectCurrentQueryText() const {
    return m_sql ? m_sql->toPlainText() : QString();
}

bool QueryPage::persist(const QString& name, const QString& sql, QString* err){
    // Se asume que DataModel expone saveQuery(name, sql, &err) y emite queriesChanged
    return DataModel::instance().saveQuery(name, sql, err);
}

void QueryPage::save(){
    const QString sql = collectCurrentQueryText().trimmed();
    if (sql.isEmpty()) {
        QMessageBox::information(this, tr("Guardar consulta"), tr("No hay SQL para guardar."));
        return;
    }
    if (currentName_.isEmpty()) {
        saveAs();
        return;
    }
    QString err;
    if (!persist(currentName_, sql, &err)) {
        QMessageBox::warning(this, tr("Guardar consulta"), err.isEmpty() ? tr("No se pudo guardar.") : err);
        return;
    }
    currentSql_ = sql;
    m_status->setText(tr("Guardado como “%1”.").arg(currentName_));
    emit saved(currentName_);
}

void QueryPage::saveAs(){
    const QString sql = collectCurrentQueryText().trimmed();
    if (sql.isEmpty()) {
        QMessageBox::information(this, tr("Guardar como…"), tr("No hay SQL para guardar."));
        return;
    }
    bool ok=false;
    QString name = QInputDialog::getText(this, tr("Guardar consulta"),
                                         tr("Nombre:"), QLineEdit::Normal,
                                         currentName_.isEmpty() ? tr("Nueva consulta") : currentName_, &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    // Si ya existe, confirmar sobreescritura
    const auto names = DataModel::instance().queries();
    bool exists = false;
    for (const auto& n : names) if (QString::compare(n, name, Qt::CaseInsensitive)==0) { exists = true; break; }
    if (exists) {
        auto ret = QMessageBox::question(this, tr("Guardar como…"),
                                         tr("“%1” ya existe. ¿Deseas reemplazarla?").arg(name),
                                         QMessageBox::Yes|QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    QString err;
    if (!persist(name, sql, &err)) {
        QMessageBox::warning(this, tr("Guardar como…"), err.isEmpty() ? tr("No se pudo guardar.") : err);
        return;
    }
    currentName_ = name;
    currentSql_  = sql;
    m_status->setText(tr("Guardado como “%1”.").arg(currentName_));
    emit savedAs(currentName_);
}

void QueryPage::loadSavedByName(const QString& name){
    if (name.trimmed().isEmpty()) return;
    // Se asume que DataModel expone querySql(name)
    const QString sql = DataModel::instance().querySql(name);
    if (sql.isEmpty()) {
        QMessageBox::warning(this, tr("Abrir consulta"), tr("No se encontró SQL para “%1”.").arg(name));
        return;
    }
    currentName_ = name;
    currentSql_  = sql;
    setSqlText(sql);
    m_status->setText(tr("Cargada “%1”.").arg(currentName_));
}
