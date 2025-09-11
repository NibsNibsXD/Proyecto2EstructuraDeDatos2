#include "recordspage.h"
#include "ui_recordspage.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QDate>
#include <QDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QIntValidator>
#include <QLabel>
#include <QTableWidgetItem>
#include <QSignalBlocker>
#include <QItemSelectionModel>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QHBoxLayout>
#include <QBrush>
#include <QColor>
#include <algorithm>
#include <QTimer>
#include <QLocale>
#include <QSet>
#include <QDebug>
#include <QStyledItemDelegate>
#include <QLocale>
#include <QRegularExpression>

static QString cleanNumericText(QString s) {
    s = s.trimmed();
    // quita símbolos típicos de moneda y espacios duros
    s.remove(QChar::Nbsp);
    s.remove(QRegularExpression("[\\s\\p{Sc}]")); // quita espacios y símbolos de moneda
    // deja solo dígitos, punto, coma y signo
    s = s.remove(QRegularExpression("[^0-9,\\.-]"));
    // si hay ambos (coma y punto), asume coma = miles → elimínala
    if (s.contains(',') && s.contains('.')) s.remove(',');
    // si solo hay coma, trátala como decimal
    else if (s.contains(',') && !s.contains('.')) s.replace(',', '.');
    return s;
}

// Helpers locales (si los usas aquí)
static int parseDecPlaces(const QString& fmt) {
    QRegularExpression rx("\\bdp=(\\d)\\b");
    auto m = rx.match(fmt);
    int dp = m.hasMatch() ? m.captured(1).toInt() : 2;
    return std::clamp(dp, 0, 4);
}
static QString baseFormatKey(const QString& fmt) {
    int i = fmt.indexOf("|dp=");
    return (i >= 0) ? fmt.left(i).trimmed() : fmt.trimmed();
}


// ---- helper de tipo (texto -> etiqueta estable) ----
static inline QString normType(const QString& t) {
    const QString s = t.trimmed().toLower();
    if (s.startsWith(u"auto")) return "autonumeracion";
    if (s.startsWith(u"número") || s.startsWith(u"numero")) return "numero";
    if (s.startsWith(u"fecha")) return "fecha_hora";
    if (s.startsWith(u"moneda")) return "moneda";
    if (s.startsWith(u"sí/no") || s.startsWith(u"si/no")) return "booleano";
    if (s.startsWith(u"texto largo")) return "texto_largo";
    return "texto"; // Texto corto
}

RecordsPage::RecordsPage(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::RecordsPage)
{
    ui->setupUi(this);

    // ---- Conexiones de encabezado ----
    connect(ui->cbTabla,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordsPage::onTablaChanged);
    connect(ui->leBuscar,   &QLineEdit::textChanged,
            this, &RecordsPage::onBuscarChanged);
    connect(ui->btnLimpiarBusqueda, &QToolButton::clicked,
            this, &RecordsPage::onLimpiarBusqueda);

    // ---- Tabla ----
    connect(ui->twRegistros, &QTableWidget::itemSelectionChanged,
            this, &RecordsPage::onSelectionChanged);
    connect(ui->twRegistros, &QTableWidget::itemDoubleClicked,
            this, &RecordsPage::onItemDoubleClicked);
    connect(ui->twRegistros, &QTableWidget::itemChanged,
            this, &RecordsPage::onItemChanged);

    // Después de crear ui->twRegistros y el resto de conexiones:
    connect(ui->twRegistros, &QTableWidget::currentCellChanged,
            this, &RecordsPage::onCurrentCellChanged);


    // Config base de la tabla
    ui->twRegistros->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->twRegistros->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->twRegistros->setEditTriggers(
        QAbstractItemView::DoubleClicked
        | QAbstractItemView::SelectedClicked
        | QAbstractItemView::EditKeyPressed);
    ui->twRegistros->setAlternatingRowColors(true);
    ui->twRegistros->setSortingEnabled(false);
    ui->twRegistros->verticalHeader()->setVisible(false);

    auto *hh = ui->twRegistros->horizontalHeader();
    hh->setStretchLastSection(false);
    hh->setSectionResizeMode(QHeaderView::Interactive);
    hh->setDefaultSectionSize(133); // ancho por columna
    hh->setSortIndicatorShown(false);
    hh->setSectionsClickable(false);

    // Limpiar “chrome”
    hideLegacyChrome();
    ui->cbTabla->clear();
    ui->twRegistros->setColumnCount(0);
    ui->twRegistros->setRowCount(0);

    setMode(Mode::Idle);
    updateStatusLabels();
    updateNavState();
}

RecordsPage::~RecordsPage()
{
    delete ui;
}

/* =================== Integración con TablesPage/Shell =================== */

void RecordsPage::setTableFromFieldDefs(const QString& name, const Schema& defs)
{
    if (m_rowsConn) { disconnect(m_rowsConn); m_rowsConn = QMetaObject::Connection(); }

    m_tableName = name;
    m_schema    = defs;

    // Refleja el nombre en el combo sin disparar onTablaChanged
    {
        QSignalBlocker block(ui->cbTabla);
        int idx = ui->cbTabla->findText(name);
        if (idx < 0) { ui->cbTabla->addItem(name); idx = ui->cbTabla->count() - 1; }
        ui->cbTabla->setCurrentIndex(idx);
    }

    if (auto *lbl = findChild<QLabel*>("lblFormTitle")) lbl->clear();

    applyDefs(defs);
    reloadRows();

    // Refrescar al vuelo si cambian filas en el modelo
    m_rowsConn = connect(&DataModel::instance(), &DataModel::rowsChanged, this,
                         [this](const QString& t){ if (t == m_tableName) reloadRows(); });

    setMode(Mode::Idle);
    updateStatusLabels();
    updateNavState();
}

/* =================== Construcción de columnas =================== */

void RecordsPage::applyDefs(const Schema& defs)
{
    ui->twRegistros->clear();
    ui->twRegistros->setRowCount(0);

    if (defs.isEmpty()) { ui->twRegistros->setColumnCount(0); return; }

    ui->twRegistros->setColumnCount(defs.size());
    QStringList headers; headers.reserve(defs.size());
    for (const auto& f : defs) headers << f.name;
    ui->twRegistros->setHorizontalHeaderLabels(headers);

    auto *hh = ui->twRegistros->horizontalHeader();
    hh->setDefaultSectionSize(133);
    for (int c = 0; c < defs.size(); ++c) {
        hh->setSectionResizeMode(c, QHeaderView::Interactive);
        ui->twRegistros->setColumnWidth(c, 133);
    }
}

QString RecordsPage::formatCell(const FieldDef& fd, const QVariant& v) const
{
    if (!v.isValid() || v.isNull()) return QString();
    const QString t = normType(fd.type);

    if (t == "fecha_hora")
        return v.toDate().toString("yyyy-MM-dd");

    if (t == "numero") {
        const QString sz = fd.autoSubtipo.trimmed().toLower();
        const bool isInt = sz.contains("byte") || sz.contains("entero") ||
                           sz.contains("integer") || sz.contains("long");
        if (isInt) {
            return QString::number(v.toLongLong());
        } else {
            const int dp = parseDecPlaces(fd.formato);   // 0..4
            return QString::number(v.toDouble(), 'f', dp);
        }
    }

    if (t == "moneda") {
        const int dp = parseDecPlaces(fd.formato);
        const QString base = baseFormatKey(fd.formato).toUpper();

        QString sym;
        if (base.startsWith("LPS") || base.startsWith("HNL") || base.startsWith("L ")) sym = "L ";
        else if (base.startsWith("USD") || base.startsWith("$"))                      sym = "$";
        else if (base.startsWith("EUR") || base.startsWith("€"))                      sym = "€";

        const QLocale loc = QLocale::system();
        return sym + loc.toString(v.toDouble(), 'f', dp);
    }

    // Fallback seguro para texto/autonumeración/otros
    return v.toString();
}


void RecordsPage::reloadRows()
{
    if (m_tableName.isEmpty() || m_schema.isEmpty()) return;

    m_isReloading = true;

    // 1) Si hubiera un editor inline activo, elimínalo con seguridad
    if (QWidget *ed = ui->twRegistros->viewport()->findChild<QWidget*>(
            "qt_editing_widget", Qt::FindChildrenRecursively)) {
        ed->hide();
        ed->deleteLater();                 // evita el overlay que tapa el "1"
    }
    // Limpia estado interno de editores
    ui->twRegistros->setItemDelegate(new QStyledItemDelegate(ui->twRegistros));

    // 2) Sorting siempre OFF mientras exista la fila (New)
    ui->twRegistros->setSortingEnabled(false);

    const auto& rowsData = DataModel::instance().rows(m_tableName);

    // 3) Preservar selección actual
    int prevSelRow = -1, prevSelCol = -1;
    if (auto *sm = ui->twRegistros->selectionModel()) {
        const auto idx = sm->currentIndex();
        prevSelRow = idx.row();
        prevSelCol = idx.column();
    }

    // 4) Reconstrucción con señales bloqueadas
    {
        QSignalBlocker block(ui->twRegistros);
        ui->twRegistros->clearContents();
        ui->twRegistros->setRowCount(0);

        for (int r = 0; r < rowsData.size(); ++r) {
            const Record& rec = rowsData[r];
            ui->twRegistros->insertRow(r);

            for (int c = 0; c < m_schema.size(); ++c) {
                const FieldDef& fd = m_schema[c];
                const QVariant  vv = (c < rec.size() ? rec[c] : QVariant());
                const QString   t  = normType(fd.type);

                if (t == "booleano") {
                    auto *wrap = new QWidget(ui->twRegistros);
                    auto *hl   = new QHBoxLayout(wrap);
                    hl->setContentsMargins(0,0,0,0);
                    auto *cb   = new QCheckBox(wrap);
                    cb->setChecked(vv.toBool());
                    hl->addWidget(cb);
                    hl->addStretch(1);
                    ui->twRegistros->setCellWidget(r, c, wrap);

                    connect(cb, &QCheckBox::checkStateChanged, this, [this, r](int){
                        if (m_isReloading || m_isCommitting) return;
                        Record rowRec = rowToRecord(r);
                        QString err;
                        if (!DataModel::instance().validate(m_schema, rowRec, &err)) { reloadRows(); return; }
                        DataModel::instance().updateRow(m_tableName, r, rowRec, &err);
                    });
                } else {
                    auto *it = new QTableWidgetItem(formatCell(fd, vv));
                    Qt::ItemFlags fl = it->flags();
                    if (fd.pk || t == "autonumeracion")
                        fl &= ~Qt::ItemIsEditable;   // ID/PK no editable
                    else
                        fl |=  Qt::ItemIsEditable;
                    it->setFlags(fl);
                    ui->twRegistros->setItem(r, c, it);
                }
            }
        }

        // 5) Triggers de edición seguros (NO AllEditTriggers)
        ui->twRegistros->setEditTriggers(
            QAbstractItemView::DoubleClicked
            | QAbstractItemView::SelectedClicked
            | QAbstractItemView::EditKeyPressed);
    }

    // 6) Restaurar selección SIN disparar señales
    if (ui->twRegistros->rowCount() > 0) {
        int selRow = std::clamp(prevSelRow, 0, ui->twRegistros->rowCount() - 1);
        int selCol = std::clamp(prevSelCol, 0, ui->twRegistros->columnCount() - 1);
        QSignalBlocker blockSel(ui->twRegistros);
        ui->twRegistros->setCurrentCell(selRow, selCol);
        ui->twRegistros->selectRow(selRow);
    }

    // 7) Defensa: jamás debe haber cellWidget en la columna ID de filas de datos
    int idCol = -1;
    for (int c = 0; c < m_schema.size(); ++c)
        if (normType(m_schema[c].type) == "autonumeracion") { idCol = c; break; }
    if (idCol >= 0) {
        const int dataRows = ui->twRegistros->rowCount();
        for (int r = 0; r < dataRows; ++r) {
            if (auto *w = ui->twRegistros->cellWidget(r, idCol))
                ui->twRegistros->removeCellWidget(r, idCol);
        }
    }

    // 8) Fila (New) con editores al final
    addNewRowEditors();

    // 9) “Pinear” el ID visible de la primera fila (por si algún editor lo tapaba)
    if (idCol >= 0) {
        const auto& vrows = DataModel::instance().rows(m_tableName);
        if (!vrows.isEmpty() && !vrows[0].isEmpty()) {
            QVariant v0 = (idCol < vrows[0].size() ? vrows[0][idCol] : QVariant());
            ui->twRegistros->removeCellWidget(0, idCol);
            QTableWidgetItem *it0 = ui->twRegistros->item(0, idCol);
            if (!it0) { it0 = new QTableWidgetItem; ui->twRegistros->setItem(0, idCol, it0); }
            it0->setFlags(it0->flags() & ~Qt::ItemIsEditable);
            it0->setData(Qt::DisplayRole, v0.isValid() && !v0.isNull() ? v0 : QVariant(1));
            // qDebug() << "ID[0] =" << v0 << " itemText=" << it0->text();
        }
    }

    // 10) No reactivar sorting aquí
    updateStatusLabels();
    updateNavState();

    m_isReloading = false;
}



/* =================== Nueva fila tipo Access =================== */

static inline QString normTypeDs(const QString& t) {
    const QString s = t.trimmed().toLower();
    if (s.startsWith(u"auto")) return "autonumeracion";
    if (s.startsWith(u"número") || s.startsWith(u"numero")) return "numero";
    if (s.startsWith(u"fecha")) return "fecha_hora";
    if (s.startsWith(u"moneda")) return "moneda";
    if (s.startsWith(u"sí/no") || s.startsWith(u"si/no")) return "booleano";
    if (s.startsWith(u"texto largo")) return "texto_largo";
    return "texto";
}

QWidget* RecordsPage::makeEditorFor(const FieldDef& fd) const
{
    const QString t = normType(fd.type);

    if (t == "autonumeracion") {
        auto *le = new QLineEdit;
        le->setReadOnly(true);
        le->setPlaceholderText("(New)");
        le->setStyleSheet("color:#777;");
        return le;
    }

    if (t == "fecha_hora") {
        auto *de = new QDateEdit;
        de->setCalendarPopup(true);
        de->setDisplayFormat("yyyy-MM-dd");
        de->setDate(QDate::currentDate());
        connect(de, &QDateEdit::dateChanged, this, &RecordsPage::prepareNextNewRow);
        return de;
    }


    if (t == "moneda") {
        auto *ds = new QDoubleSpinBox;
        ds->setDecimals(2);
        ds->setRange(-1e12, 1e12);
        ds->setButtonSymbols(QAbstractSpinBox::NoButtons);
        connect(ds, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &RecordsPage::prepareNextNewRow);
        connect(ds, &QDoubleSpinBox::editingFinished, this, &RecordsPage::commitNewRow);
        return ds;
    }

    if (t == "booleano") {
        auto *cb = new QCheckBox;
        cb->setTristate(false);
        connect(cb, &QCheckBox::checkStateChanged, this, &RecordsPage::prepareNextNewRow);
        connect(cb, &QCheckBox::checkStateChanged, this, &RecordsPage::commitNewRow);
        // lo devolvemos tal cual; abajo soportamos tanto wrap como directo
        return cb;
    }

    if (t == "texto_largo") {
        auto *le = new QLineEdit; // inline simple
        connect(le, &QLineEdit::textEdited, this, &RecordsPage::prepareNextNewRow);
        connect(le, &QLineEdit::returnPressed, this, &RecordsPage::commitNewRow);
        return le;
    }

    // texto corto / número
    auto *le = new QLineEdit;
    if (fd.size > 0) le->setMaxLength(fd.size);
    connect(le, &QLineEdit::textEdited, this, &RecordsPage::prepareNextNewRow);
    connect(le, &QLineEdit::editingFinished, this, &RecordsPage::commitNewRow);
    return le;
}


QVariant RecordsPage::editorValue(QWidget* w, const QString& t) const
{
    const QString typ = normType(t);

    if (typ == "booleano") {
        if (auto *cb = qobject_cast<QCheckBox*>(w)) return cb->isChecked();
        if (auto *cb2 = w->findChild<QCheckBox*>())   return cb2->isChecked();
        return false;
    }
    if (typ == "autonumeracion") {
        if (auto *le = qobject_cast<QLineEdit*>(w)) return le->text().trimmed();
        return QVariant();
    }
    if (typ == "fecha_hora") {
        if (auto *de = qobject_cast<QDateEdit*>(w)) return de->date();
        return QVariant();
    }
    if (typ == "moneda") {
        if (auto *ds = qobject_cast<QDoubleSpinBox*>(w)) return ds->value();
        return QVariant();
    }
    if (auto *le = qobject_cast<QLineEdit*>(w)) return le->text();
    return QVariant();
}


void RecordsPage::clearNewRowEditors()
{
    int last = ui->twRegistros->rowCount() - 1;
    if (last < 0) return;
    for (int c = 0; c < ui->twRegistros->columnCount(); ++c) {
        if (auto *w = ui->twRegistros->cellWidget(last, c)) {
            if (auto *le = qobject_cast<QLineEdit*>(w)) le->clear();
            else if (auto *cb = qobject_cast<QCheckBox*>(w)) cb->setChecked(false);
            else if (auto *chk = w->findChild<QCheckBox*>()) chk->setChecked(false);
            else if (auto *de  = qobject_cast<QDateEdit*>(w)) de->setDate(QDate::currentDate());
            else if (auto *ds  = qobject_cast<QDoubleSpinBox*>(w)) ds->setValue(0.0);
        }
    }
}


void RecordsPage::addNewRowEditors(qint64 presetId)
{
    if (m_schema.isEmpty()) return;

    const int newRow = ui->twRegistros->rowCount();
    ui->twRegistros->insertRow(newRow);

    // localizar la primera columna autonumeración (ID)
    int idCol = -1;
    for (int c = 0; c < m_schema.size(); ++c) {
        if (normType(m_schema[c].type) == "autonumeracion") { idCol = c; break; }
    }

    // Preasigna el ID real y deja la celda NO editable (sin cellWidget)
    if (idCol >= 0) {
        qint64 idVal;
        if (presetId >= 0) {
            idVal = presetId;
        } else {
            // Primera (New) tras recargar: consulta al modelo una sola vez
            idVal = DataModel::instance().nextAutoNumber(m_tableName).toLongLong();
        }
        auto *it = new QTableWidgetItem;
        it->setData(Qt::DisplayRole, static_cast<qlonglong>(idVal)); // pinta como número, no texto
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        ui->twRegistros->setItem(newRow, idCol, it);
    }

    // Resto de columnas: editores inline
    for (int c = 0; c < m_schema.size(); ++c) {
        if (c == idCol) continue;
        const FieldDef& fd = m_schema[c];
        QWidget* ed = makeEditorFor(fd);
        ui->twRegistros->setCellWidget(newRow, c, ed);
    }
}



void RecordsPage::commitNewRow()
{
    if (m_tableName.isEmpty() || m_schema.isEmpty()) return;
    if (m_isCommitting || m_isReloading) return;  // ← NUEVO

    m_isCommitting = true;                        // ← NUEVO

    const int last   = ui->twRegistros->rowCount() - 1;
    const int rowIns = m_preparedNextNew ? (last - 1) : last;
    if (rowIns < 0) { m_isCommitting = false; return; }

    Record r; r.resize(m_schema.size());
    for (int c = 0; c < m_schema.size(); ++c) {
        const FieldDef& fd = m_schema[c];
        const QString t = normType(fd.type);

        if (t == "autonumeracion") {
            QVariant v;
            if (auto *it = ui->twRegistros->item(rowIns, c)) v = it->text();
            else v = DataModel::instance().nextAutoNumber(m_tableName);
            r[c] = v;
        } else {
            QWidget* w = ui->twRegistros->cellWidget(rowIns, c);
            r[c] = editorValue(w, fd.type);
        }
    }

    QString err;
    if (!DataModel::instance().insertRow(m_tableName, r, &err)) {
        QMessageBox::warning(this, tr("No se pudo insertar"), err);
        m_isCommitting = false;    // importantísimo: salir limpiando el flag
        return;
    }


    m_preparedNextNew = false;
    reloadRows();                                   // seguro ahora
    const int rows = ui->twRegistros->rowCount();
    if (rows >= 2) ui->twRegistros->setCurrentCell(rows - 2, 0);

    m_isCommitting = false;                         // ← NUEVO
}



/* =================== Helpers de estado/UI =================== */

void RecordsPage::setMode(Mode m)
{
    m_mode = m;
    updateHeaderButtons();
}

void RecordsPage::updateHeaderButtons()
{
    // no hay toolbar visible
}

void RecordsPage::updateStatusLabels()
{
    if (auto *lbl = findChild<QLabel*>("lblTotal"))  lbl->clear();
    if (auto *lbl = findChild<QLabel*>("lblPagina")) lbl->clear();
}

/* =================== Acciones de encabezado =================== */

void RecordsPage::onTablaChanged(int /*index*/)
{
    if (m_tableName.isEmpty()) {
        ui->twRegistros->clear();
        ui->twRegistros->setRowCount(0);
        ui->twRegistros->setColumnCount(0);
        ui->leBuscar->clear();
        setMode(Mode::Idle);
        updateStatusLabels();
        updateNavState();
    }
}

void RecordsPage::onBuscarChanged(const QString& text)
{
    aplicarFiltroBusqueda(text);
    updateStatusLabels();
    updateNavState();
}

void RecordsPage::onLimpiarBusqueda()
{
    ui->leBuscar->clear();
    ui->leBuscar->setFocus();
}

/* ============ CRUD (slots vivos pero ocultos en UI) ============ */

bool RecordsPage::editRecordDialog(const QString& title, const Schema& s, Record& r, bool isInsert, QString* errMsg)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    auto *vl  = new QVBoxLayout(&dlg);
    auto *fl  = new QFormLayout;
    vl->addLayout(fl);

    if (r.size() < s.size()) r.resize(s.size());

    QVector<QWidget*> editors; editors.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        const FieldDef& fd = s[i];
        const QString t = normType(fd.type);
        QWidget* w = nullptr;

        if (t == "autonumeracion") {
            auto *le = new QLineEdit; le->setReadOnly(true); le->setPlaceholderText("[auto]");
            if (r[i].isValid() && !r[i].isNull()) le->setText(r[i].toString());
            w = le;
        } else if (t == "numero") {
            auto *le = new QLineEdit; le->setValidator(new QIntValidator(le));
            if (r[i].isValid() && !r[i].isNull()) le->setText(QString::number(r[i].toLongLong()));
            w = le;
        } else if (t == "moneda") {
            auto *ds = new QDoubleSpinBox; ds->setDecimals(2); ds->setRange(-1e12, 1e12);
            if (r[i].isValid() && !r[i].isNull()) ds->setValue(r[i].toDouble());
            w = ds;
        } else if (t == "fecha_hora") {
            auto *de = new QDateEdit; de->setCalendarPopup(true); de->setDisplayFormat("yyyy-MM-dd");
            if (r[i].canConvert<QDate>() && r[i].toDate().isValid()) de->setDate(r[i].toDate());
            else de->setDate(QDate::currentDate());
            w = de;
        } else if (t == "booleano") {
            auto *cb = new QCheckBox("Sí"); cb->setChecked(r[i].toBool()); w = cb;
        } else if (t == "texto_largo") {
            auto *pt = new QPlainTextEdit; pt->setPlainText(r[i].toString()); w = pt;
        } else { // texto corto
            auto *le = new QLineEdit; le->setText(r[i].toString()); w = le;
        }

        editors << w;
        fl->addRow(s[i].name + ":", w);
    }

    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    vl->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return false;

    for (int i = 0; i < s.size(); ++i) {
        const QString t = normType(s[i].type);
        QWidget* w = editors[i];
        QVariant v;
        if (t == "autonumeracion") v = r[i];
        else if (auto *le = qobject_cast<QLineEdit*>(w)) v = le->text();
        else if (auto *ds = qobject_cast<QDoubleSpinBox*>(w)) v = ds->value();
        else if (auto *de = qobject_cast<QDateEdit*>(w)) v = de->date();
        else if (auto *cb = qobject_cast<QCheckBox*>(w)) v = cb->isChecked();
        else if (auto *pt = qobject_cast<QPlainTextEdit*>(w)) v = pt->toPlainText();
        r[i] = v;
    }

    if (!DataModel::instance().validate(s, r, errMsg)) return false;
    return true;
}

/* =================== Tabla / selección =================== */

void RecordsPage::onSelectionChanged() { updateHeaderButtons(); }
void RecordsPage::onItemDoubleClicked(QTableWidgetItem * /*item*/) { onEditar(); }

/* =================== Editor legacy (oculto) =================== */
void RecordsPage::limpiarFormulario() {}
void RecordsPage::cargarFormularioDesdeFila(int) {}
void RecordsPage::escribirFormularioEnFila(int) {}
int  RecordsPage::agregarFilaDesdeFormulario() { return -1; }

/* =================== Búsqueda =================== */

bool RecordsPage::filaCoincideBusqueda(int row, const QString& term) const
{
    if (term.trimmed().isEmpty()) return true;
    const QString t = term.trimmed().toLower();
    const int cols = ui->twRegistros->columnCount();
    for (int c = 0; c < cols; ++c) {
        if (auto *it = ui->twRegistros->item(row, c)) {
            if (it->text().toLower().contains(t)) return true;
        }
    }
    return false;
}

void RecordsPage::aplicarFiltroBusqueda(const QString& term)
{
    const int rows = ui->twRegistros->rowCount();
    for (int r = 0; r < rows; ++r) {
        bool show = filaCoincideBusqueda(r, term);
        ui->twRegistros->setRowHidden(r, !show);
    }
}

/* =================== Diálogo CRUD (toolbar oculto) =================== */
void RecordsPage::onInsertar() {}
void RecordsPage::onEditar()   {}
void RecordsPage::onEliminar() {}
void RecordsPage::onGuardar()  {}
void RecordsPage::onCancelar() {}

/* =================== Navegación (oculta) =================== */
void RecordsPage::navFirst() {}
void RecordsPage::navPrev()  {}
void RecordsPage::navNext()  {}
void RecordsPage::navLast()  {}

void RecordsPage::updateNavState()   { emit navState(0, 0, false, false); }
QList<int> RecordsPage::visibleRows() const { return {}; }
int  RecordsPage::selectedVisibleIndex() const { return -1; }
void RecordsPage::selectVisibleByIndex(int) {}
int  RecordsPage::currentSortColumn() const { return ui->twRegistros->currentColumn(); }

/* =================== “Datasheet mínimo”: ocultar controles legacy =================== */
void RecordsPage::hideLegacyChrome()
{
    auto hideByObjectName = [this](const char* name){
        if (auto *w = this->findChild<QWidget*>(name)) w->hide();
    };

    // 1) Búsqueda (line edit + X)
    hideByObjectName("leBuscar");
    hideByObjectName("btnLimpiarBusqueda");

    // 2) Panel derecho (editor)
    for (auto *sp : findChildren<QSplitter*>()) {
        if (sp->count() >= 2) {
            if (auto *right = sp->widget(1)) right->hide();
            sp->setSizes({1, 0});
        }
    }
    hideByObjectName("lblFormTitle");
    hideByObjectName("editorRight");
    hideByObjectName("editorPanel");
    hideByObjectName("editorScroll");
    hideByObjectName("frmEditor");
    hideByObjectName("gbEditor");
    hideByObjectName("widgetEditor");
    hideByObjectName("btnLimpiarForm");
    hideByObjectName("btnGenerarDummy");

    // 3) Toolbar CRUD y “Ver eliminados”
    hideByObjectName("btnInsertar");
    hideByObjectName("btnEditar");
    hideByObjectName("btnEliminar");
    hideByObjectName("btnGuardar");
    hideByObjectName("btnCancelar");
    for (auto *cb : findChildren<QCheckBox*>()) {
        const auto txt = cb->text().toLower();
        if (txt.contains("eliminad")) cb->hide();
    }

    // 4) Paginación y estado inferior
    hideByObjectName("btnPrimero");
    hideByObjectName("btnAnterior");
    hideByObjectName("btnSiguiente");
    hideByObjectName("btnUltimo");
    hideByObjectName("lblPagina");
    hideByObjectName("lblTotal");
    hideByObjectName("lblStatus");
    hideByObjectName("lblEstado");

    // Barrido por patrones
    for (auto *w : findChildren<QWidget*>()) {
        const QString obj = w->objectName().toLower();
        if (obj.contains("status") || obj.contains("pagina") || obj.contains("total"))
            w->hide();
        if (obj.contains("editor"))
            w->hide();
    }
}

/* =================== Edición inline de filas existentes =================== */

Record RecordsPage::rowToRecord(int row) const
{
    Record rec; rec.resize(m_schema.size());
    for (int c = 0; c < m_schema.size(); ++c) {
        const FieldDef& fd = m_schema[c];
        const QString t = normType(fd.type);

        if (QWidget *w = ui->twRegistros->cellWidget(row, c)) {
            rec[c] = editorValue(w, fd.type);
            continue;
        }

        QTableWidgetItem *it = ui->twRegistros->item(row, c);
        const QString txt = it ? it->text() : QString();

        if (t == "fecha_hora") {
            rec[c] = QDate::fromString(txt, "yyyy-MM-dd");
        } else if (t == "moneda" || t == "numero") {
            const QString norm = cleanNumericText(txt);
            bool ok = false; double d = norm.toDouble(&ok);
            rec[c] = (ok ? QVariant(d) : QVariant());
        } else if (t == "booleano") {
            rec[c] = false; // suele venir como cellWidget
        } else if (t == "autonumeracion") {
            rec[c] = txt;

        } else { // texto / texto_largo
            rec[c] = txt;
        }
    }
    return rec;
}

void RecordsPage::onItemChanged(QTableWidgetItem* it)
{
    if (!it || m_tableName.isEmpty() || m_schema.isEmpty()) return;
    if (m_isReloading || m_isCommitting) return;

    const int row  = it->row();
    const int col  = it->column();
    const int last = ui->twRegistros->rowCount() - 1;

    if (row == last) return;
    if (normType(m_schema[col].type) == "autonumeracion") return;

    // Evita reentradas mientras actualizas
    m_isCommitting = true;
    QSignalBlocker guard(ui->twRegistros);

    Record r = rowToRecord(row);

    QString err;
    if (!DataModel::instance().updateRow(m_tableName, row, r, &err)) {
        // restaurar solo la celda editada
        const auto rows = DataModel::instance().rows(m_tableName);
        if (row >= 0 && row < rows.size() && col < rows[row].size()) {
            const FieldDef& fd = m_schema[col];
            const QVariant& vv = rows[row][col];
            if (auto *item = ui->twRegistros->item(row, col)) {
                item->setText(formatCell(fd, vv));
                item->setBackground(QColor("#ffe0e0"));
                QTimer::singleShot(650, this, [this,row,col](){
                    if (auto *it2 = ui->twRegistros->item(row,col))
                        it2->setBackground(Qt::NoBrush);
                });
            }
        }
        QMessageBox::warning(this, tr("No se pudo guardar"), err);
    } else {
        if (auto *okItem = ui->twRegistros->item(row, col))
            okItem->setBackground(Qt::NoBrush);
    }

    m_isCommitting = false;
}




/* =================== Slots legacy expuestos =================== */

void RecordsPage::onPrimero()  { if (ui->twRegistros->rowCount() > 0) ui->twRegistros->selectRow(0); }
void RecordsPage::onAnterior() { int r = ui->twRegistros->currentRow(); if (r > 0) ui->twRegistros->selectRow(r - 1); }
void RecordsPage::onSiguiente(){ int r = ui->twRegistros->currentRow(); int last = ui->twRegistros->rowCount() - 1; if (r >= 0 && r < last) ui->twRegistros->selectRow(r + 1); }
void RecordsPage::onUltimo()   { int last = ui->twRegistros->rowCount() - 1; if (last >= 0) ui->twRegistros->selectRow(last); }

void RecordsPage::sortAscending()  {}
void RecordsPage::sortDescending() {}
void RecordsPage::clearSorting()   {}


void RecordsPage::onLimpiarFormulario() { limpiarFormulario(); }
void RecordsPage::onGenerarDummyFila()  {}

void RecordsPage::prepareNextNewRow()
{
    if (m_preparedNextNew) return;
    if (ui->twRegistros->rowCount() <= 0) return;

    // Guarda la celda activa
    const int curRow = ui->twRegistros->currentRow();
    const int curCol = ui->twRegistros->currentColumn();

    // Última fila existente (la (New) actual)
    const int prevRow = ui->twRegistros->rowCount() - 1;

    // Encontrar columna ID
    int idCol = -1;
    for (int c = 0; c < m_schema.size(); ++c) {
        if (normType(m_schema[c].type) == "autonumeracion") { idCol = c; break; }
    }

    // Calcular el ID de la nueva (New) como (ID de la (New) actual) + 1
    qint64 nextIdPreset = -1;
    if (idCol >= 0) {
        if (auto *it = ui->twRegistros->item(prevRow, idCol)) {
            bool ok = false;
            const qint64 lastNewId = it->text().toLongLong(&ok);
            if (ok) nextIdPreset = lastNewId + 1;
        }
    }

    // Evitar señales durante la inserción
    QSignalBlocker block(ui->twRegistros);

    // Insertar nueva fila (New) con ID preasignado incremental
    addNewRowEditors(nextIdPreset);

    // Restaurar foco donde estaba el usuario
    if (curRow >= 0 && curCol >= 0) {
        ui->twRegistros->setCurrentCell(curRow, curCol);
        if (QWidget *ed = ui->twRegistros->cellWidget(curRow, curCol))
            ed->setFocus();
    }

    m_preparedNextNew = true;
}


void RecordsPage::onCurrentCellChanged(int currentRow, int, int previousRow, int)
{
    if (m_isReloading || m_isCommitting) return;               // guard
    const int last = ui->twRegistros->rowCount() - 1;
    if (previousRow >= 0 && currentRow >= 0
        && currentRow != previousRow
        && previousRow == last)
    {
        if (!m_preparedNextNew) return;    // ⟵ NO confirmar si la (New) estaba vacía
        commitNewRow();
    }
}


