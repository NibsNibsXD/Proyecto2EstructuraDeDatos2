#include "accessquerydesigner.h"
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
#include <QInputDialog>
#include <QDate>
#include <QRegularExpression>

// ===== Helpers de valores / normalización =====
static QString up(const QString& s){ return QString(s).toUpper(); }

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
static int schemaFieldIndex(const Schema& s, const QString& name){
    const QString n = normalizeColToken(name);
    for (int i=0;i<s.size();++i) if(s[i].name.compare(n, Qt::CaseInsensitive)==0) return i;
    return -1;
}
static int cmpVar(const QVariant& a, const QVariant& b){
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
static bool equalVar(const QVariant& a, const QVariant& b){
    if(!a.isValid() && !b.isValid()) return true;
    return cmpVar(a,b)==0;
}
static QVariant parseLiteral(const QString& tok){
    QString s=tok.trimmed();
    if(s.startsWith('\'') && s.endsWith('\'')) return s.mid(1, s.size()-2);
    if(up(s)=="TRUE")  return true;
    if(up(s)=="FALSE") return false;
    if(up(s)=="NULL")  return QVariant();
    bool ok=false;
    qlonglong i=s.toLongLong(&ok); if(ok) return QVariant::fromValue(i);
    double d=s.toDouble(&ok);      if(ok) return d;
    QDate dt=QDate::fromString(s, "yyyy-MM-dd"); if(dt.isValid()) return dt;
    return s;
}
static QToolButton* mkBtn(const QString& text){
    auto *b = new QToolButton;
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return b;
}

// =======================================================================

AccessQueryDesignerPage::AccessQueryDesignerPage(QWidget* parent) : QWidget(parent){
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(8,8,8,8);
    root->setSpacing(6);

    // ===== Barra superior
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

    // ===== Centro: Campos + Grid tipo Access
    auto mid = new QHBoxLayout; mid->setSpacing(10);

    // Izquierda: lista de campos y botones de columnas
    auto leftCol = new QVBoxLayout;
    leftCol->addWidget(new QLabel("Campos"));
    lwFields_ = new QListWidget; lwFields_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    leftCol->addWidget(lwFields_, 1);

    auto bAdd    = mkBtn("Añadir al grid →");
    auto bRemove = mkBtn("Quitar col");
    auto bClear  = mkBtn("Limpiar grid");
    auto bLeft   = mkBtn("←");
    auto bRight  = mkBtn("→");

    auto moveRow = new QHBoxLayout;
    moveRow->addWidget(bAdd);
    moveRow->addWidget(bRemove);
    moveRow->addWidget(bClear);
    moveRow->addStretch();
    leftCol->addLayout(moveRow);

    // Derecha: herramientas + grid + ORDER/LIMIT
    auto rightCol = new QVBoxLayout;

    // Operadores
    auto tools = new QHBoxLayout;
    tools->addWidget(new QLabel("Operadores:"));
    auto bEq = mkBtn("="), bNe = mkBtn("<>"), bGt = mkBtn(">"), bLt = mkBtn("<"),
        bGe = mkBtn(">="), bLe = mkBtn("<="), bLike = mkBtn("LIKE"),
        bNotLike = mkBtn("NOT LIKE"), bBetween = mkBtn("BETWEEN"),
        bIn = mkBtn("IN (…)"), bNotIn = mkBtn("NOT IN (…)"),
        bIsNull = mkBtn("IS NULL"), bNotNull = mkBtn("IS NOT NULL"),
        bTrue = mkBtn("TRUE"), bFalse = mkBtn("FALSE"),
        bQuotes = mkBtn(" '…' "), bParens = mkBtn("( … )");
    for (auto *b : {bEq,bNe,bGt,bLt,bGe,bLe,bLike,bNotLike,bBetween,bIn,bNotIn,bIsNull,bNotNull,bTrue,bFalse,bQuotes,bParens})
        tools->addWidget(b);

    // Grid: N filas (OR); columnas dinámicas por campo añadido
    grid_ = new QTableWidget(2, 0);
    grid_->setHorizontalHeaderLabels(QStringList()); // los headers son los nombres de campo
    grid_->horizontalHeader()->setStretchLastSection(true);
    grid_->verticalHeader()->setVisible(true);
    grid_->setVerticalHeaderLabels(QStringList() << "Criteria" << "Or");
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);
    grid_->setSelectionBehavior(QAbstractItemView::SelectItems);
    grid_->setEditTriggers(QAbstractItemView::AllEditTriggers);

    // Fila de utilidades (OR+, OR-)
    auto orRow = new QHBoxLayout;
    auto bAddOr = mkBtn("+ OR");
    auto bDelOr = mkBtn("− OR");
    orRow->addWidget(bAddOr);
    orRow->addWidget(bDelOr);
    orRow->addStretch();

    // Orden + Limit + preview
    auto bottom = new QHBoxLayout;
    bottom->addWidget(new QLabel("ORDER BY:"));
    cbOrderBy_ = new QComboBox;
    btnDesc_   = mkBtn("DESC"); btnDesc_->setCheckable(true);
    bottom->addWidget(cbOrderBy_, 0);
    bottom->addWidget(btnDesc_, 0);

    bottom->addSpacing(16);
    bottom->addWidget(new QLabel("LIMIT:"));
    spLimit_ = new QSpinBox; spLimit_->setRange(0, 1000000); spLimit_->setValue(0);
    bottom->addWidget(spLimit_, 0);

    bottom->addSpacing(12);
    bottom->addWidget(new QLabel("Preview SQL:"));
    sqlPreview_ = new QLabel; sqlPreview_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sqlPreview_->setStyleSheet("font-family:monospace; color:#444;");
    bottom->addWidget(sqlPreview_, 1);

    status_ = new QLabel; status_->setStyleSheet("color:#666");

    rightCol->addLayout(tools);
    rightCol->addWidget(grid_, 1);
    rightCol->addLayout(orRow);
    rightCol->addLayout(bottom);

    mid->addLayout(leftCol, 0);
    mid->addLayout(rightCol, 1);

    // Resultados embebidos (abajo)
    results_ = new QTableWidget;
    results_->setObjectName("designerResults");
    results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_->horizontalHeader()->setStretchLastSection(true);
    results_->setMinimumHeight(200);

    root->addLayout(top);
    root->addLayout(mid, 0);
    root->addWidget(results_, 1);
    root->addWidget(status_);

    // Conexiones
    connect(cbTable_, &QComboBox::currentTextChanged, this, &AccessQueryDesignerPage::onTableChanged);
    connect(lwFields_, &QListWidget::itemDoubleClicked, [this](QListWidgetItem*){ onAddSelectedField(); });
    connect(bAdd,    &QToolButton::clicked, this, &AccessQueryDesignerPage::onAddSelectedField);
    connect(bRemove, &QToolButton::clicked, this, &AccessQueryDesignerPage::onRemoveSelectedColumn);
    connect(bClear,  &QToolButton::clicked, this, &AccessQueryDesignerPage::onClearGrid);
    connect(bLeft,   &QToolButton::clicked, this, &AccessQueryDesignerPage::onMoveLeft);
    connect(bRight,  &QToolButton::clicked, this, &AccessQueryDesignerPage::onMoveRight);

    connect(bEq, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpEq);
    connect(bNe, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpNe);
    connect(bGt, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpGt);
    connect(bLt, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpLt);
    connect(bGe, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpGe);
    connect(bLe, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpLe);
    connect(bLike, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpLike);
    connect(bNotLike, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpNotLike);
    connect(bBetween, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpBetween);
    connect(bIn, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpIn);
    connect(bNotIn, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertOpNotIn);
    connect(bIsNull, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertIsNull);
    connect(bNotNull, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertNotNull);
    connect(bTrue, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertTrue);
    connect(bFalse, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertFalse);
    connect(bQuotes, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertQuotes);
    connect(bParens, &QToolButton::clicked, this, &AccessQueryDesignerPage::onInsertParens);

    connect(bAddOr, &QToolButton::clicked, this, &AccessQueryDesignerPage::onAddOrRow);
    connect(bDelOr, &QToolButton::clicked, this, &AccessQueryDesignerPage::onDelOrRow);

    connect(bRun,    &QToolButton::clicked, this, &AccessQueryDesignerPage::onRun);
    connect(bSave,   &QToolButton::clicked, this, &AccessQueryDesignerPage::onSave);
    connect(bSaveAs, &QToolButton::clicked, this, &AccessQueryDesignerPage::onSaveAs);
    connect(bRen,    &QToolButton::clicked, this, &AccessQueryDesignerPage::onRename);
    connect(bDel,    &QToolButton::clicked, this, &AccessQueryDesignerPage::onDelete);

    // Inicial
    onTableChanged(cbTable_->currentText());
}

// ========== API pública ==========
void AccessQueryDesignerPage::setName(const QString& name){ if (edName_) edName_->setText(name); }
void AccessQueryDesignerPage::setSqlText(const QString& sql){ lastSqlText_ = sql; if (sqlPreview_) sqlPreview_->setText(sql); }

QString AccessQueryDesignerPage::currentTable() const {
    return cbTable_ ? cbTable_->currentText() : QString();
}

// ========== helpers internos ==========
int AccessQueryDesignerPage::criteriaRowCount() const { return grid_ ? grid_->rowCount() : 0; }

void AccessQueryDesignerPage::ensureGridHeaders(){
    // Fila 0 se llama "Criteria"; el resto "Or", "Or 2", ...
    QStringList rows;
    if (grid_->rowCount() > 0) rows << "Criteria";
    for (int r=1; r<grid_->rowCount(); ++r) rows << (r==1 ? "Or" : QString("Or %1").arg(r));
    grid_->setVerticalHeaderLabels(rows);
}

// ========== Campos / grid ==========
void AccessQueryDesignerPage::rebuildFields(){
    lwFields_->clear();
    cbOrderBy_->clear();

    const Schema s = DataModel::instance().schema(currentTable());
    for (const auto& f : s) {
        lwFields_->addItem(f.name);
        cbOrderBy_->addItem(f.name);
    }
}

void AccessQueryDesignerPage::onTableChanged(const QString&){
    rebuildFields();
    onClearGrid();
    if (results_) { results_->setRowCount(0); results_->setColumnCount(0); }
    if (status_) status_->clear();
}

void AccessQueryDesignerPage::onAddSelectedField(){
    const auto sel = lwFields_->selectedItems();
    if (sel.isEmpty()) return;
    for (auto *it : sel) {
        const int col = grid_->columnCount();
        grid_->insertColumn(col);
        grid_->setHorizontalHeaderItem(col, new QTableWidgetItem(it->text()));
        for (int r=0;r<grid_->rowCount();++r){
            if (!grid_->item(r,col)) grid_->setItem(r,col,new QTableWidgetItem);
        }
    }
    if (sqlPreview_) sqlPreview_->setText(buildSql());
}

void AccessQueryDesignerPage::onRemoveSelectedColumn(){
    auto items = grid_->selectedItems();
    if (items.isEmpty()) return;
    int col = items.first()->column();
    grid_->removeColumn(col);
    if (sqlPreview_) sqlPreview_->setText(buildSql());
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
    if (sqlPreview_) sqlPreview_->setText(buildSql());
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
    if (sqlPreview_) sqlPreview_->setText(buildSql());
}
void AccessQueryDesignerPage::onClearGrid(){
    grid_->clear();
    grid_->setRowCount(2);
    grid_->setColumnCount(0);
    ensureGridHeaders();
    if (sqlPreview_) sqlPreview_->setText(buildSql());
}

void AccessQueryDesignerPage::onAddOrRow(){
    grid_->insertRow(grid_->rowCount());
    ensureGridHeaders();
    // Crear celdas editables en cada columna existente
    for (int c=0; c<grid_->columnCount(); ++c)
        if (!grid_->item(grid_->rowCount()-1, c)) grid_->setItem(grid_->rowCount()-1, c, new QTableWidgetItem);
    if (sqlPreview_) sqlPreview_->setText(buildSql());
}

void AccessQueryDesignerPage::onDelOrRow(){
    if (grid_->rowCount() <= 1) return; // siempre dejamos al menos 1 fila
    int r = grid_->currentRow();
    if (r < 1) r = grid_->rowCount()-1; // no borrar la fila 0 "Criteria" para evitar líos
    if (r < 1) return;
    grid_->removeRow(r);
    ensureGridHeaders();
    if (sqlPreview_) sqlPreview_->setText(buildSql());
}

void AccessQueryDesignerPage::insertIntoActiveCriteriaCell(const QString& text){
    auto items = grid_->selectedItems();
    int row = 0, col = 0;
    if (!items.isEmpty()){ row = items.first()->row(); col = items.first()->column(); }
    if (row<0) row=0;
    if (col<0) return;
    if (row >= grid_->rowCount() || col >= grid_->columnCount()) return;
    auto *it = grid_->item(row,col);
    if (!it){ it = new QTableWidgetItem; grid_->setItem(row,col,it); }
    QString cur = it->text();
    if (!cur.isEmpty() && !cur.endsWith(' ') && !text.startsWith(')')) cur += ' ';
    it->setText(cur + text);
    if (sqlPreview_) sqlPreview_->setText(buildSql());
}

void AccessQueryDesignerPage::onInsertOpEq(){ insertIntoActiveCriteriaCell("="); }
void AccessQueryDesignerPage::onInsertOpNe(){ insertIntoActiveCriteriaCell("<>"); }
void AccessQueryDesignerPage::onInsertOpGt(){ insertIntoActiveCriteriaCell(">"); }
void AccessQueryDesignerPage::onInsertOpLt(){ insertIntoActiveCriteriaCell("<"); }
void AccessQueryDesignerPage::onInsertOpGe(){ insertIntoActiveCriteriaCell(">="); }
void AccessQueryDesignerPage::onInsertOpLe(){ insertIntoActiveCriteriaCell("<="); }
void AccessQueryDesignerPage::onInsertOpLike(){ insertIntoActiveCriteriaCell("LIKE "); }
void AccessQueryDesignerPage::onInsertOpNotLike(){ insertIntoActiveCriteriaCell("NOT LIKE "); }
void AccessQueryDesignerPage::onInsertOpBetween(){ insertIntoActiveCriteriaCell("BETWEEN  AND "); }
void AccessQueryDesignerPage::onInsertOpIn(){ insertIntoActiveCriteriaCell("IN ('')"); }
void AccessQueryDesignerPage::onInsertOpNotIn(){ insertIntoActiveCriteriaCell("NOT IN ('')"); }
void AccessQueryDesignerPage::onInsertIsNull(){ insertIntoActiveCriteriaCell("IS NULL"); }
void AccessQueryDesignerPage::onInsertNotNull(){ insertIntoActiveCriteriaCell("IS NOT NULL"); }
void AccessQueryDesignerPage::onInsertTrue(){ insertIntoActiveCriteriaCell("TRUE"); }
void AccessQueryDesignerPage::onInsertFalse(){ insertIntoActiveCriteriaCell("FALSE"); }
void AccessQueryDesignerPage::onInsertQuotes(){ insertIntoActiveCriteriaCell("''"); }
void AccessQueryDesignerPage::onInsertParens(){ insertIntoActiveCriteriaCell("( )"); }

// ===== SQL =====
QString AccessQueryDesignerPage::buildSql() const{
    const QString table = currentTable();
    if (table.isEmpty()) return "";

    // SELECT
    QStringList cols;
    for (int c=0;c<grid_->columnCount();++c){
        auto *hdr = grid_->horizontalHeaderItem(c);
        if (!hdr) continue;
        const QString field = hdr->text().trimmed();
        if (field.isEmpty()) continue;
        cols << QString("%1.%2").arg(table, field);
    }
    const QString select = cols.isEmpty() ? "*" : cols.join(", ");

    // WHERE (N filas OR; dentro de cada fila, AND)
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

    QStringList orRows;
    for (int r=0; r<grid_->rowCount(); ++r) {
        QString rr = rowExpr(r);
        if (!rr.isEmpty()) orRows << "(" + rr + ")";
    }

    QString sql = "SELECT " + select + " FROM " + table;
    if(!orRows.isEmpty()) sql += " WHERE " + orRows.join(" OR ");

    // ORDER BY
    if (cbOrderBy_ && cbOrderBy_->count()>0 && !cbOrderBy_->currentText().trimmed().isEmpty()) {
        sql += " ORDER BY " + table + "." + cbOrderBy_->currentText().trimmed();
        if (btnDesc_ && btnDesc_->isChecked()) sql += " DESC";
    }

    int lim = spLimit_ ? spLimit_->value() : 0;
    if(lim>0) sql += " LIMIT " + QString::number(lim);

    return sql + ";";
}

// ===== Ejecutar y pintar resultados =====
void AccessQueryDesignerPage::onRun(){
    auto& dm = DataModel::instance();
    const QString table = currentTable();
    const Schema schema = dm.schema(table);
    if (schema.isEmpty()) { QMessageBox::warning(this, "SELECT", "Tabla no encontrada"); return; }

    // columnas a mostrar
    QStringList cols;
    for (int c=0;c<grid_->columnCount();++c){
        auto *hdr = grid_->horizontalHeaderItem(c);
        if (hdr && !hdr->text().trimmed().isEmpty()) cols << hdr->text().trimmed();
    }
    if (cols.isEmpty()) for (const auto& f : schema) cols << f.name;

    // condiciones sencillas (todas las filas OR)
    struct Cond { QString col; QString op; QVariant val; };
    using CondRow = QVector<Cond>;
    QVector<CondRow> whereRows;

    auto parseCond = [&](const QString& field, const QString& t)->CondRow{
        QString s = t.trimmed();
        if (s.isEmpty()) return {};
        // operadores binarios comunes
        static const QStringList ops = {"<>","!=",">=","<=","=","<",">"};
        for (const QString& op : ops) {
            if (s.startsWith(op)) {
                QString rhs = s.mid(op.size()).trimmed();
                return { { field, op=="!=" ? "<>" : op, parseLiteral(rhs) } };
            }
        }
        // IS NULL / NOT NULL / TRUE / FALSE
        if (up(s)=="IS NULL")     return { { field, "=", QVariant() } };
        if (up(s)=="IS NOT NULL") return {}; // no lo filtramos explícitamente (se deja pasar todos)
        if (up(s)=="TRUE")        return { { field, "=", true } };
        if (up(s)=="FALSE")       return { { field, "=", false } };

        // BETWEEN a AND b
        {
            QRegularExpression re("^BETWEEN\\s+(.+)\\s+AND\\s+(.+)$", QRegularExpression::CaseInsensitiveOption);
            auto m = re.match(s);
            if (m.hasMatch()) {
                QVariant a = parseLiteral(m.captured(1).trimmed());
                QVariant b = parseLiteral(m.captured(2).trimmed());
                // Modelamos como (>= a) y (<= b)
                return { {field, ">=", a}, {field, "<=", b} };
            }
        }

        // IN ( … )
        {
            QRegularExpression re("^NOT\\s+IN\\s*\\((.+)\\)$", QRegularExpression::CaseInsensitiveOption);
            auto m = re.match(s);
            if (m.hasMatch()) {
                QStringList toks = m.captured(1).split(',', Qt::SkipEmptyParts);
                CondRow cr;
                // NOT IN lo modelamos como != para cada elemento y luego AND
                for (auto t : toks) cr << Cond{field, "<>", parseLiteral(t.trimmed())};
                return cr;
            }
        }
        {
            QRegularExpression re("^IN\\s*\\((.+)\\)$", QRegularExpression::CaseInsensitiveOption);
            auto m = re.match(s);
            if (m.hasMatch()) {
                QStringList toks = m.captured(1).split(',', Qt::SkipEmptyParts);
                // IN lo modelamos como OR de iguales; aquí devolvemos un truco:
                // el ejecutor de abajo sólo entiende AND dentro de fila,
                // así que simplificamos a "igual a alguno" replicando filas no es trivial.
                // Para mantenerlo simple, si hay IN, dejaremos pasar y el preview no filtrará por IN.
                // (El SQL generado sí tendrá IN porque lo insertamos tal cual.)
                return {}; // no aplicamos filtro local; se verá en la ejecución real si la hay
            }
        }

        // LIKE / NOT LIKE: tampoco hacemos el patrón en preview local; lo omitimos.
        if (s.startsWith("LIKE ", Qt::CaseInsensitive) ||
            s.startsWith("NOT LIKE ", Qt::CaseInsensitive)) {
            return {};
        }

        // genérico: igualdad del literal completo
        return { { field, "=", parseLiteral(s) } };
    };

    for (int r=0; r<grid_->rowCount(); ++r){
        CondRow rowConds;
        for (int c=0;c<grid_->columnCount();++c){
            auto *hdr = grid_->horizontalHeaderItem(c);
            if (!hdr) continue;
            const QString field = hdr->text().trimmed();
            if (field.isEmpty()) continue;

            auto *crit = grid_->item(r,c);
            if (!crit) continue;
            const QString cond = crit->text().trimmed();
            if (cond.isEmpty()) continue;

            for (const auto& cd : parseCond(field, cond)) rowConds << cd;
        }
        if (!rowConds.isEmpty()) whereRows << rowConds;
    }

    // header resultados
    results_->clear(); results_->setRowCount(0);
    results_->setColumnCount(cols.size());
    QStringList hdr; for (const QString& c : cols) hdr << (table + "." + c);
    results_->setHorizontalHeaderLabels(hdr);

    auto matchRowAND = [&](const Record& r, const QVector<Cond>& andConds)->bool{
        for (const auto& c : andConds) {
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
    auto matchRowOR = [&](const Record& r)->bool{
        if (whereRows.isEmpty()) return true;
        for (const auto& andGroup : whereRows)
            if (matchRowAND(r, andGroup)) return true;
        return false;
    };

    const auto& data = dm.rows(table);
    QVector<const Record*> rows; rows.reserve(data.size());
    for (const auto& r : data) if (!r.isEmpty() && matchRowOR(r)) rows << &r;

    // ORDER BY (local, si es posible)
    QString orderCol = cbOrderBy_ ? cbOrderBy_->currentText().trimmed() : QString();
    bool orderDesc = btnDesc_ && btnDesc_->isChecked();
    if (!orderCol.isEmpty()){
        int oi = schemaFieldIndex(schema, orderCol);
        if (oi >= 0) {
            std::sort(rows.begin(), rows.end(), [&](const Record* a, const Record* b){
                int c = cmpVar(a->value(oi), b->value(oi));
                return orderDesc ? (c>0) : (c<0);
            });
        }
    }

    // LIMIT
    int limit = spLimit_ && spLimit_->value() > 0 ? spLimit_->value() : -1;
    int take = rows.size(); if (limit>=0) take = qMin(take, limit);

    // volcado
    results_->setRowCount(take);
    for (int r=0; r<take; ++r){
        for (int c=0; c<cols.size(); ++c){
            int ix = schemaFieldIndex(schema, cols[c]);
            if (ix<0) continue;
            results_->setItem(r,c,new QTableWidgetItem(rows[r]->value(ix).toString()));
        }
    }

    const QString sql = buildSql();
    if (sqlPreview_) sqlPreview_->setText(sql);
    if (status_) status_->setText(QString::number(take) + " fila(s) — " + sql);
    emit runSql(sql);
}

// ===== Persistencia =====
void AccessQueryDesignerPage::onSave(){
    QString name = edName_->text().trimmed();
    if (name.isEmpty()) { QMessageBox::warning(this,"Guardar","Ponle un nombre a la consulta."); return; }
    QString err;
    const QString sql = buildSql();
    if (!DataModel::instance().saveQuery(name, sql, &err)) {
        QMessageBox::warning(this,"Guardar", err.isEmpty()? "No se pudo guardar." : err);
        return;
    }
    if (sqlPreview_) sqlPreview_->setText(sql);
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
    const QString oldName = edName_->text().trimmed();
    if (oldName.isEmpty()) { QMessageBox::warning(this,"Renombrar","Primero escribe el nombre actual."); return; }
    bool ok=false;
    const QString nn = QInputDialog::getText(this,"Renombrar","Nuevo nombre:", QLineEdit::Normal, oldName, &ok).trimmed();
    if(!ok || nn.isEmpty() || nn==oldName) return;

    const QString oldSql = DataModel::instance().querySql(oldName);
    if (oldSql.isEmpty()) { QMessageBox::warning(this,"Renombrar","No se encontró la consulta original."); return; }

    QString err;
    if (!DataModel::instance().saveQuery(nn, oldSql, &err)) {
        QMessageBox::warning(this,"Renombrar", err.isEmpty()? "No se pudo crear la nueva consulta." : err);
        return;
    }
    if (!DataModel::instance().removeQuery(oldName, &err)) {
        QMessageBox::warning(this,"Renombrar", err.isEmpty()? "La nueva consulta se creó, pero no se pudo borrar la anterior." : err);
        return;
    }
    edName_->setText(nn);
    emit savedQuery(nn);
    QMessageBox::information(this,"Renombrar","Consulta renombrada.");
}
void AccessQueryDesignerPage::onDelete(){
    const QString name = edName_->text().trimmed();
    if (name.isEmpty()) return;
    if (QMessageBox::question(this,"Eliminar","¿Eliminar la consulta?")==QMessageBox::No) return;
    QString err;
    if (!DataModel::instance().removeQuery(name, &err)) {
        QMessageBox::warning(this,"Eliminar", err.isEmpty()? "No se pudo eliminar." : err);
        return;
    }
    emit savedQuery(name);
    QMessageBox::information(this,"Eliminar","Consulta eliminada.");
}
