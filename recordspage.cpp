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
#include <QToolTip>
#include <QCursor>
#include <QRegularExpression>
#include <QDoubleValidator>
#include <QRegularExpressionValidator>
#include <QKeyEvent>
#include <QClipboard>
#include <QKeySequence>
#include <QGuiApplication>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QCalendarWidget>
#include <QCoreApplication>

static QString cleanNumericText(QString s) {
    s = s.trimmed();
    s.remove(QChar::Nbsp);
    s.remove(QRegularExpression("[\\s\\p{Sc}]"));
    s = s.remove(QRegularExpression("[^0-9,\\.-]"));
    if (s.contains(',') && s.contains('.')) s.remove(',');
    else if (s.contains(',') && !s.contains('.')) s.replace(',', '.');
    return s;
}

static int parseDecPlaces(const QString& fmt) {
    QRegularExpression rx("\\bdp=(\\d)\\b");
    auto m = rx.match(fmt);
    int dp = m.hasMatch() ? m.captured(1).toInt() : 2;
    return std::clamp(dp, 0, 4);
}

static void showCellTip(RecordsPage* owner,
                        const QModelIndex& idx,
                        const QString& msg,
                        int msec = 1800)
{
    if (!owner) return;
    auto *view = owner->sheet();
    QRect r = idx.isValid() ? view->visualRect(idx)
                            : QRect(view->viewport()->mapFromGlobal(QCursor::pos()), QSize(1,1));
    QToolTip::showText(view->viewport()->mapToGlobal(r.bottomRight()), msg, view, r, msec);
}

class NumInputFilter : public QObject {
public:
    NumInputFilter(QLineEdit* le, RecordsPage* owner, const QModelIndex& idx,
                   bool isInt, int dp, QObject* parent=nullptr)
        : QObject(parent), m_le(le), m_owner(owner), m_idx(idx),
        m_isInt(isInt), m_dp(std::clamp(dp,0,4)) {}

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj != m_le) return QObject::eventFilter(obj, ev);

        if (ev->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent*>(ev);
            const int k = ke->key();

            if (ke->matches(QKeySequence::Paste) ||
                (ke->modifiers() == Qt::ShiftModifier && ke->key() == Qt::Key_Insert)) {
                const QString clip = QGuiApplication::clipboard()->text();
                if (!accepts(clip)) {
                    showCellTip(m_owner, m_idx,
                                m_isInt ? QObject::tr("Solo enteros (sin decimales).")
                                        : QObject::tr("Solo números con hasta %1 decimales.").arg(m_dp));
                    return true;
                }
                insertNormalized(clip);
                return true;
            }

            if (ke->modifiers() & (Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier)) return false;
            if (k==Qt::Key_Backspace || k==Qt::Key_Delete ||
                k==Qt::Key_Left || k==Qt::Key_Right ||
                k==Qt::Key_Home || k==Qt::Key_End || k==Qt::Key_Tab || k==Qt::Key_Return || k==Qt::Key_Enter)
                return false;

            const QString t = ke->text();
            if (t.isEmpty()) return false;

            if (t.contains(QRegularExpression("[A-Za-z]"))) {
                showCellTip(m_owner, m_idx, QObject::tr("Solo números."));
                return true;
            }

            if (t == "-") {
                if (m_le->cursorPosition()!=0 || m_le->text().contains('-')) {
                    showCellTip(m_owner, m_idx, QObject::tr("El signo '-' solo al inicio."));
                    return true;
                }
                return false;
            }

            if (t == "." || t == ",") {
                if (m_isInt) {
                    showCellTip(m_owner, m_idx, QObject::tr("Este campo es entero; no admite decimales."));
                    return true;
                }
                insertNormalized(".");
                return true;
            }

            if (t.contains(QRegularExpression("\\d"))) {
                if (!m_isInt && m_dp >= 0) {
                    QString next = prospectiveText(t);
                    const int dot = next.indexOf('.');
                    if (dot >= 0) {
                        const int after = next.mid(dot+1).remove(QRegularExpression("[^0-9]")).size();
                        if (after > m_dp) {
                            showCellTip(m_owner, m_idx,
                                        QObject::tr("Máximo %1 decimales.").arg(m_dp));
                            return true;
                        }
                    }
                }
                return false;
            }

            showCellTip(m_owner, m_idx, QObject::tr("Solo números."));
            return true;
        }

        return QObject::eventFilter(obj, ev);
    }

private:
    QString prospectiveText(const QString& in) const {
        QString cur = m_le->text();
        const int selStart = m_le->selectionStart();
        if (selStart >= 0) {
            cur.remove(selStart, m_le->selectedText().size());
            cur.insert(selStart, in);
        } else {
            cur.insert(m_le->cursorPosition(), in);
        }
        return cur;
    }

    bool accepts(const QString& chunk) const {
        QString in = chunk;
        in.replace(',', '.');
        QString next = prospectiveText(in);

        if (next.contains(QRegularExpression("[A-Za-z]"))) return false;
        if (m_isInt && next.contains('.')) return false;

        if (!m_isInt) {
            int dot = next.indexOf('.');
            if (dot >= 0) {
                int after = next.mid(dot+1).remove(QRegularExpression("[^0-9]")).size();
                if (after > m_dp) return false;
            }
        }
        int minusCount = next.count('-');
        if (minusCount > 1) return false;
        if (minusCount==1 && !next.startsWith('-')) return false;

        return true;
    }

    void insertNormalized(QString s) {
        s.replace(',', '.');
        QString next = prospectiveText(s);
        m_le->setText(next);
        m_le->setCursorPosition(next.size());
        emit m_le->textEdited(next);
    }

    QLineEdit*    m_le;
    RecordsPage*  m_owner;
    QModelIndex   m_idx;
    bool          m_isInt{false};
    int           m_dp{2};
};

static QString baseFormatKey(const QString& fmt) {
    int i = fmt.indexOf("|dp=");
    return (i >= 0) ? fmt.left(i).trimmed() : fmt.trimmed();
}

static inline QString normType(const QString& t) {
    const QString s = t.trimmed().toLower();
    if (s.startsWith(u"auto")) return "autonumeracion";
    if (s.startsWith(u"número") || s.startsWith(u"numero")) return "numero";
    if (s.startsWith(u"fecha")) return "fecha_hora";
    if (s.startsWith(u"moneda")) return "moneda";
    if (s.startsWith(u"sí/no") || s.startsWith(u"si/no")) return "booleano";
    if (s.startsWith(u"texto largo")) return "texto_largo";
    return "texto";
}

class DatePopupOpener : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        auto *de = qobject_cast<QDateEdit*>(obj);
        if (!de) return QObject::eventFilter(obj, ev);

        if (ev->type() == QEvent::FocusIn || ev->type() == QEvent::MouseButtonPress) {
            QTimer::singleShot(0, de, [de]{
                QKeyEvent press(QEvent::KeyPress, Qt::Key_F4, Qt::NoModifier);
                QCoreApplication::sendEvent(de, &press);
                QKeyEvent release(QEvent::KeyRelease, Qt::Key_F4, Qt::NoModifier);
                QCoreApplication::sendEvent(de, &release);
            });
        }
        return QObject::eventFilter(obj, ev);
    }
};

class DatasheetDelegate : public QStyledItemDelegate {
public:
    explicit DatasheetDelegate(RecordsPage* owner)
        : QStyledItemDelegate(owner), m_owner(owner) {}

    QWidget* createEditor(QWidget *parent,
                          const QStyleOptionViewItem &,
                          const QModelIndex &idx) const override
    {
        QLineEdit *le = new QLineEdit(parent);
        le->setFrame(false);
#ifdef Q_OS_MAC
        le->setAttribute(Qt::WA_MacShowFocusRect, false);
#endif
        le->setStyleSheet("margin:0; padding:2px 4px;");

        if (m_owner && idx.isValid()) {
            const Schema& s = m_owner->schema();
            if (idx.column() >= 0 && idx.column() < s.size()) {
                const FieldDef& fd = s[idx.column()];
                const QString t = normType(fd.type);

                if ((t == "texto" || t == "texto_largo") && fd.size > 0) {
                    le->setMaxLength(fd.size);
                    QObject::connect(le, &QLineEdit::textEdited, le, [this, le, fd, idx]{
                        if (le->text().size() == fd.size && m_owner) {
                            const QRect r = m_owner->sheet()->visualRect(idx);
                            QToolTip::showText(
                                m_owner->sheet()->viewport()->mapToGlobal(r.bottomRight()),
                                QObject::tr("Solo se aceptan %1 caracteres para \"%2\".")
                                    .arg(fd.size).arg(fd.name),
                                m_owner->sheet(), r, 2000);
                        }
                    });
                }

                if (t == "numero") {
                    const QString sz = fd.autoSubtipo.trimmed().toLower();
                    const bool isInt = sz.contains("byte") || sz.contains("entero")
                                       || sz.contains("integer") || sz.contains("long");
                    if (isInt) {
                        auto *rxv = new QRegularExpressionValidator(
                            QRegularExpression(QStringLiteral("^-?\\d{0,19}$")), le);
                        le->setValidator(rxv);
                        le->setPlaceholderText(QObject::tr("Solo enteros"));
                        auto *f = new NumInputFilter(le, m_owner, idx, true, 0, le);
                        le->installEventFilter(f);
                    } else {
                        const int dp = std::clamp(parseDecPlaces(fd.formato), 0, 4);
                        auto *dv = new QDoubleValidator(le);
                        dv->setDecimals(dp);
                        dv->setNotation(QDoubleValidator::StandardNotation);
                        dv->setRange(-1e12, 1e12);
                        le->setValidator(dv);
                        le->setPlaceholderText(QObject::tr("Número con hasta %1 decimales").arg(dp));
                        auto *f = new NumInputFilter(le, m_owner, idx, false, dp, le);
                        le->installEventFilter(f);
                    }
                }

                if (t == "moneda") {
                    const int dp = std::clamp(parseDecPlaces(fd.formato), 0, 4);
                    auto *dv = new QDoubleValidator(le);
                    dv->setDecimals(dp);
                    dv->setNotation(QDoubleValidator::StandardNotation);
                    dv->setRange(-1e12, 1e12);
                    le->setValidator(dv);
                    le->setPlaceholderText(QObject::tr("Monto con hasta %1 decimales").arg(dp));
                    auto *f = new NumInputFilter(le, m_owner, idx, false, dp, le);
                    le->installEventFilter(f);
                }

                if (t == "fecha_hora") {
                    auto *de = new QDateEdit(parent);
                    const QString base = baseFormatKey(fd.formato);
                    de->setDisplayFormat(base.isEmpty() ? "dd/MM/yy" : base);
                    de->setLocale(QLocale(QLocale::Spanish, QLocale::Honduras));
                    de->setCalendarPopup(true);
                    de->setFrame(false);
                    de->setKeyboardTracking(false);
                    de->setFocusPolicy(Qt::StrongFocus);
                    de->setSpecialValueText("");
                    de->setMinimumDate(QDate(100, 1, 1));
                    de->setDate(QDate::currentDate());

                    de->setButtonSymbols(QAbstractSpinBox::NoButtons);
                    if (auto *led = de->findChild<QLineEdit*>()) {
                        led->setReadOnly(true);
                        led->setContextMenuPolicy(Qt::NoContextMenu);
                    }
                    struct NoTypeFilter : QObject {
                        using QObject::QObject;
                        bool eventFilter(QObject *o, QEvent *e) override {
                            if (e->type() == QEvent::Wheel) return true;
                            if (e->type() == QEvent::KeyPress) {
                                auto *k = static_cast<QKeyEvent*>(e);
                                if (k->key()==Qt::Key_Tab || k->key()==Qt::Key_Backtab || k->key()==Qt::Key_F4)
                                    return QObject::eventFilter(o,e);
                                return true;
                            }
                            return QObject::eventFilter(o,e);
                        }
                    };
                    de->installEventFilter(new NoTypeFilter(de));

                    de->setProperty("touched", false);
                    QObject::connect(de, &QDateEdit::dateChanged, de, [de]{ de->setProperty("touched", true); });
                    QObject::connect(de, &QDateEdit::editingFinished, de, [de]{
                        if (de->date().isValid() && de->date() != de->minimumDate())
                            de->setProperty("touched", true);
                    });
                    if (de->calendarPopup()) {
                        if (auto *cal = de->calendarWidget()) {
                            QObject::connect(cal, &QCalendarWidget::clicked,   de, [de](const QDate& d){
                                de->setDate(d); de->setProperty("touched", true);
                            });
                            QObject::connect(cal, &QCalendarWidget::activated, de, [de](const QDate& d){
                                de->setDate(d); de->setProperty("touched", true);
                            });
                        }
                    }
                    de->installEventFilter(new DatePopupOpener(de));
                    return de;
                }
            }
        }

        if (auto *view = qobject_cast<QTableView*>(parent->parent())) {
            const int want = le->sizeHint().height() + 2;
            if (view->rowHeight(idx.row()) < want)
                view->setRowHeight(idx.row(), want);
        }
        return le;
    }

    void setEditorData(QWidget *editor, const QModelIndex &idx) const override {
        if (auto *de = qobject_cast<QDateEdit*>(editor)) {
            const QString txt = m_owner->sheet()->item(idx.row(), idx.column())
            ? m_owner->sheet()->item(idx.row(), idx.column())->text()
            : QString();

            QDate val = QDate::fromString(txt, de->displayFormat());
            de->blockSignals(true);
            if (val.isValid()) {
                de->setDate(val);
                de->setProperty("touched", false);
            } else {
                de->setDate(QDate::currentDate());
                de->setProperty("touched", true);
            }
            de->blockSignals(false);
            return;
        }

        if (auto *le = qobject_cast<QLineEdit*>(editor)) {
            if (!m_owner || !idx.isValid()) {
                QStyledItemDelegate::setEditorData(editor, idx);
                return;
            }
            const FieldDef& fd = m_owner->schema()[idx.column()];
            const QString t = normType(fd.type);
            if (t == "numero" || t == "moneda") {
                QTableWidgetItem *it = m_owner->sheet()->item(idx.row(), idx.column());
                const QString raw = it ? it->text() : QString();
                const QString cleaned = cleanNumericText(raw);
                le->setText(cleaned);
                le->setCursorPosition(le->text().size());
                return;
            }
        }
        QStyledItemDelegate::setEditorData(editor, idx);
    }

    void updateEditorGeometry(QWidget *editor,
                              const QStyleOptionViewItem &opt,
                              const QModelIndex &) const override
    {
        editor->setGeometry(opt.rect.adjusted(1, 1, -1, -1));
    }

private:
    RecordsPage* m_owner;
};

RecordsPage::RecordsPage(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::RecordsPage)
{
    ui->setupUi(this);

    connect(ui->cbTabla,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordsPage::onTablaChanged);
    connect(ui->leBuscar,   &QLineEdit::textChanged,
            this, &RecordsPage::onBuscarChanged);
    connect(ui->btnLimpiarBusqueda, &QToolButton::clicked,
            this, &RecordsPage::onLimpiarBusqueda);

    connect(ui->twRegistros, &QTableWidget::itemSelectionChanged,
            this, &RecordsPage::onSelectionChanged);
    connect(ui->twRegistros, &QTableWidget::itemDoubleClicked,
            this, &RecordsPage::onItemDoubleClicked);
    connect(ui->twRegistros, &QTableWidget::itemChanged,
            this, &RecordsPage::onItemChanged);
    connect(ui->twRegistros, &QTableWidget::currentCellChanged,
            this, &RecordsPage::onCurrentCellChanged);

    ui->twRegistros->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->twRegistros->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->twRegistros->setEditTriggers(
        QAbstractItemView::CurrentChanged
        | QAbstractItemView::SelectedClicked
        | QAbstractItemView::DoubleClicked
        | QAbstractItemView::EditKeyPressed);
    ui->twRegistros->setAlternatingRowColors(true);
    ui->twRegistros->setSortingEnabled(false);
    ui->twRegistros->verticalHeader()->setVisible(false);

    auto *hh = ui->twRegistros->horizontalHeader();
    ui->twRegistros->setItemDelegate(new DatasheetDelegate(this));
    ui->twRegistros->setWordWrap(false);
    ui->twRegistros->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->twRegistros->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    auto *hh2 = ui->twRegistros->horizontalHeader();
    connect(hh2, &QHeaderView::sectionDoubleClicked, this,
            [this](int c){ ui->twRegistros->resizeColumnToContents(c); });
    hh->setStretchLastSection(false);
    hh->setSectionResizeMode(QHeaderView::Interactive);
    hh->setDefaultSectionSize(133);
    hh->setSortIndicatorShown(false);
    hh->setSectionsClickable(false);

    hideLegacyChrome();
    ui->cbTabla->clear();
    ui->twRegistros->setColumnCount(0);
    ui->twRegistros->setRowCount(0);

    setMode(RecordsPage::Mode::Idle);
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

    {
        QSignalBlocker block(ui->cbTabla);
        ui->cbTabla->clear();
        ui->cbTabla->addItems(DataModel::instance().tables());
        const int idx = ui->cbTabla->findText(name);
        ui->cbTabla->setCurrentIndex(idx);
    }

    if (auto *lbl = findChild<QLabel*>("lblFormTitle")) lbl->clear();

    applyDefs(defs);
    reloadRows();

    m_rowsConn = connect(&DataModel::instance(), &DataModel::rowsChanged, this,
                         [this](const QString& t){ if (t == m_tableName) reloadRows(); });

    setMode(RecordsPage::Mode::Idle);
    updateStatusLabels();
    updateNavState();

    auto& dm = DataModel::instance();

    connect(&dm, &DataModel::tableCreated, this, [this](const QString& t) {
        if (ui->cbTabla->findText(t) < 0) ui->cbTabla->addItem(t);
    });

    connect(&dm, &DataModel::tableDropped, this, [this](const QString& t) {
        int idx = ui->cbTabla->findText(t);
        if (idx >= 0) ui->cbTabla->removeItem(idx);

        if (t == m_tableName) {
            if (m_rowsConn) { disconnect(m_rowsConn); m_rowsConn = QMetaObject::Connection(); }
            m_tableName.clear();
            m_schema.clear();
            ui->twRegistros->clear();
            ui->twRegistros->setRowCount(0);
            ui->twRegistros->setColumnCount(0);
            ui->leBuscar->clear();
            setMode(RecordsPage::Mode::Idle);
            updateStatusLabels();
            updateNavState();
        }
    });

    connect(&dm, &DataModel::schemaChanged, this, [this](const QString& t, const Schema& s) {
        if (t == m_tableName) {
            m_schema = s;
            applyDefs(s);
            reloadRows();
        }
    });
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

    if (t == "fecha_hora") {
        const QDate d = v.toDate();
        if (!d.isValid()) return QString();

        const QString base = baseFormatKey(fd.formato);
        if (base == "dd-MM-yy")        return d.toString("dd-MM-yy");
        if (base == "dd/MM/yy")        return d.toString("dd/MM/yy");
        if (base == "dd/MMMM/yyyy") {
            QLocale es(QLocale::Spanish, QLocale::Honduras);
            return es.toString(d, "dd/MMMM/yyyy");
        }
        return d.toString("dd/MM/yy");
    }

    if (t == "numero") {
        const QString sz = fd.autoSubtipo.trimmed().toLower();
        const bool isInt = sz.contains("byte") || sz.contains("entero") ||
                           sz.contains("integer") || sz.contains("long");
        if (isInt) {
            return QString::number(v.toLongLong());
        } else {
            const int dp = parseDecPlaces(fd.formato);
            return QString::number(v.toDouble(), 'f', dp);
        }
    }

    if (t == "moneda") {
        const int dp = parseDecPlaces(fd.formato);
        QString base = baseFormatKey(fd.formato).toUpper();
        if (base.isEmpty()) base = "LPS";

        QString sym;
        if (base.startsWith("LPS") || base.startsWith("HNL") || base.startsWith("L ")) sym = "L ";
        else if (base.startsWith("USD") || base.startsWith("$"))                      sym = "$";
        else if (base.startsWith("EUR") || base.startsWith("€"))                      sym = "€";
        else if (base.startsWith("MILLARES") || base.startsWith("M"))                 sym = "M ";

        const QLocale loc = QLocale::system();
        return sym + loc.toString(v.toDouble(), 'f', dp);
    }

    return v.toString();
}

void RecordsPage::reloadRows()
{
    if (m_tableName.isEmpty() || m_schema.isEmpty()) return;

    const auto tables = DataModel::instance().tables();
    if (!tables.contains(m_tableName)) {
        m_schema.clear();
        ui->twRegistros->clear();
        ui->twRegistros->setRowCount(0);
        ui->twRegistros->setColumnCount(0);
        setMode(RecordsPage::Mode::Idle);
        updateStatusLabels();
        updateNavState();
        return;
    }

    m_isReloading = true;

    auto oldTriggers = ui->twRegistros->editTriggers();
    ui->twRegistros->setEditTriggers(QAbstractItemView::NoEditTriggers);

    const auto editors = ui->twRegistros->viewport()->findChildren<QWidget*>(
        "qt_editing_widget", Qt::FindChildrenRecursively);
    for (QWidget *ed : editors) {
        ed->blockSignals(true);
        ed->hide();
        ed->deleteLater();
    }

    for (int r = 0; r < ui->twRegistros->rowCount(); ++r) {
        for (int c = 0; c < ui->twRegistros->columnCount(); ++c) {
            if (QWidget *w = ui->twRegistros->cellWidget(r, c)) {
                ui->twRegistros->removeCellWidget(r, c);
                w->deleteLater();
            }
        }
    }

    ui->twRegistros->setItemDelegate(new DatasheetDelegate(this));
    ui->twRegistros->setSortingEnabled(false);

    const auto& rowsData = DataModel::instance().rows(m_tableName);

    int prevSelRow = -1, prevSelCol = -1;
    if (auto *sm = ui->twRegistros->selectionModel()) {
        const auto idx = sm->currentIndex();
        prevSelRow = idx.row();
        prevSelCol = idx.column();
    }

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
                        fl &= ~Qt::ItemIsEditable;
                    else
                        fl |=  Qt::ItemIsEditable;
                    it->setFlags(fl);
                    it->setToolTip(formatCell(fd, vv));
                    ui->twRegistros->setItem(r, c, it);
                }
            }
        }

        ui->twRegistros->setEditTriggers(
            QAbstractItemView::CurrentChanged
            | QAbstractItemView::SelectedClicked
            | QAbstractItemView::DoubleClicked
            | QAbstractItemView::EditKeyPressed);
    }

    if (ui->twRegistros->rowCount() > 0) {
        int selRow = std::clamp(prevSelRow, 0, ui->twRegistros->rowCount() - 1);
        int selCol = std::clamp(prevSelCol, 0, ui->twRegistros->columnCount() - 1);
        QSignalBlocker blockSel(ui->twRegistros);
        ui->twRegistros->setCurrentCell(selRow, selCol);
        ui->twRegistros->selectRow(selRow);
    }

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

    addNewRowEditors();

    if (idCol >= 0) {
        const auto& vrows = DataModel::instance().rows(m_tableName);
        if (!vrows.isEmpty() && !vrows[0].isEmpty()) {
            QVariant v0 = (idCol < vrows[0].size() ? vrows[0][idCol] : QVariant());
            ui->twRegistros->removeCellWidget(0, idCol);
            QTableWidgetItem *it0 = ui->twRegistros->item(0, idCol);
            if (!it0) { it0 = new QTableWidgetItem; ui->twRegistros->setItem(0, idCol, it0); }
            it0->setFlags(it0->flags() & ~Qt::ItemIsEditable);
            it0->setData(Qt::DisplayRole, v0.isValid() && !v0.isNull() ? v0 : QVariant(1));
        }
    }

    updateStatusLabels();
    updateNavState();

    QTimer::singleShot(0, this, [this]{
        const auto editors2 = ui->twRegistros->viewport()->findChildren<QWidget*>(
            "qt_editing_widget", Qt::FindChildrenRecursively);
        for (QWidget *ed : editors2) {
            ed->blockSignals(true);
            ed->hide();
            ed->deleteLater();
        }
    });

    ui->twRegistros->setEditTriggers(oldTriggers);
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
        const QString base = baseFormatKey(fd.formato);
        de->setDisplayFormat(base.isEmpty() ? "dd/MM/yy" : base);
        de->setLocale(QLocale(QLocale::Spanish, QLocale::Honduras));
        de->setSpecialValueText("");
        de->setMinimumDate(QDate(100,1,1));
        de->setDate(QDate::currentDate());
        de->setKeyboardTracking(false);
        de->setFocusPolicy(Qt::StrongFocus);

        de->setProperty("touched", false);
        connect(de, &QDateEdit::dateChanged, de, [de]{ de->setProperty("touched", true); });

        auto *self = const_cast<RecordsPage*>(this);
        connect(de, &QDateEdit::dateChanged, self, &RecordsPage::prepareNextNewRow);

        if (de->calendarPopup()) {
            if (auto *cal = de->calendarWidget()) {
                connect(cal, &QCalendarWidget::clicked, self, [self, de](const QDate& d){
                    de->setDate(d);
                    de->setProperty("touched", true);
                    QTimer::singleShot(0, self, [self]{ self->commitNewRow(); });
                });
                connect(cal, &QCalendarWidget::activated, self, [self, de](const QDate& d){
                    de->setDate(d);
                    de->setProperty("touched", true);
                    QTimer::singleShot(0, self, [self]{ self->commitNewRow(); });
                });
            }
        }
        return de;
    }

    if (t == "moneda") {
        auto *le = new QLineEdit;
        const int dp = std::clamp(parseDecPlaces(fd.formato), 0, 4);
        auto *dv = new QDoubleValidator(le);
        dv->setDecimals(dp);
        dv->setNotation(QDoubleValidator::StandardNotation);
        dv->setRange(-1e12, 1e12);
        le->setValidator(dv);
        le->setPlaceholderText(tr("Monto con hasta %1 decimales").arg(dp));
        auto *f = new NumInputFilter(le, const_cast<RecordsPage*>(this),
                                     QModelIndex(), /*isInt=*/false, dp, le);
        le->installEventFilter(f);
        connect(le, &QLineEdit::textEdited,      this, &RecordsPage::prepareNextNewRow);
        connect(le, &QLineEdit::editingFinished, this, &RecordsPage::commitNewRow);
        return le;
    }

    if (t == "booleano") {
        auto *cb = new QCheckBox;
        cb->setTristate(false);
        connect(cb, &QCheckBox::checkStateChanged, this, &RecordsPage::prepareNextNewRow);
        connect(cb, &QCheckBox::checkStateChanged, this, &RecordsPage::commitNewRow);
        return cb;
    }

    if (t == "numero") {
        auto *le = new QLineEdit;
        const bool isInt = fd.autoSubtipo.trimmed().toLower().contains("byte")
                           || fd.autoSubtipo.trimmed().toLower().contains("entero")
                           || fd.autoSubtipo.trimmed().toLower().contains("integer")
                           || fd.autoSubtipo.trimmed().toLower().contains("long");

        if (isInt) {
            le->setValidator(new QRegularExpressionValidator(
                QRegularExpression(QStringLiteral("^-?\\d{0,19}$")), le));
            le->setPlaceholderText(tr("Solo enteros"));
        } else {
            const int dp = std::clamp(parseDecPlaces(fd.formato), 0, 4);
            auto *dv = new QDoubleValidator(le);
            dv->setDecimals(dp);
            dv->setNotation(QDoubleValidator::StandardNotation);
            dv->setRange(-1e12, 1e12);
            le->setValidator(dv);
            le->setPlaceholderText(tr("Número con hasta %1 decimales").arg(dp));
        }

        connect(le, &QLineEdit::textEdited,      this, &RecordsPage::prepareNextNewRow);
        connect(le, &QLineEdit::editingFinished, this, &RecordsPage::commitNewRow);

        const int dp = std::clamp(parseDecPlaces(fd.formato), 0, 4);
        auto *f = new NumInputFilter(le, const_cast<RecordsPage*>(this),
                                     QModelIndex(), isInt, isInt ? 0 : dp, le);
        le->installEventFilter(f);
        return le;
    }

    auto *le = new QLineEdit;
    if (normType(fd.type) == "texto" && fd.size > 0) {
        connect(le, &QLineEdit::textEdited, this,
                [this, le, max = fd.size, field = fd.name](const QString& s) {
                    if (s.size() > max) {
                        const QString cut = s.left(max);
                        QSignalBlocker b(le);
                        le->setText(cut);
                        le->setCursorPosition(cut.size());
                        QToolTip::showText(
                            le->mapToGlobal(QPoint(le->width(), le->height())),
                            tr("Solo se aceptan %1 caracteres para \"%2\".\nSe recortó el texto.")
                                .arg(max).arg(field),
                            le, le->rect(), 2000);
                    }
                });
    }
    connect(le, &QLineEdit::textEdited,      this, &RecordsPage::prepareNextNewRow);
    connect(le, &QLineEdit::editingFinished, this, &RecordsPage::commitNewRow);
    return le;
}

QVariant RecordsPage::editorValue(QWidget* w, const QString& t) const
{
    const QString typ = normType(t);

    if (typ == "booleano") {
        if (auto *cb = qobject_cast<QCheckBox*>(w)) return cb->isChecked();
        if (auto *cb2 = w->findChild<QCheckBox*>()) return cb2->isChecked();
        return false;
    }
    if (typ == "autonumeracion") {
        if (auto *le = qobject_cast<QLineEdit*>(w)) return le->text().trimmed();
        return QVariant();
    }
    if (typ == "fecha_hora") {
        if (auto *de = qobject_cast<QDateEdit*>(w)) {
            const bool touched = de->property("touched").toBool();
            if (!touched) return QVariant();
            return QVariant(de->date());
        }
        if (auto *le = qobject_cast<QLineEdit*>(w)) {
            QLocale es(QLocale::Spanish, QLocale::Honduras);
            const QString s = le->text().trimmed();
            QDate d = QDate::fromString(s, "dd-MM-yy");
            if (!d.isValid()) d = QDate::fromString(s, "dd/MM/yy");
            if (!d.isValid()) d = es.toDate(s, "dd/MMMM/yyyy");
            return d.isValid() ? QVariant(d) : QVariant();
        }
        return QVariant();
    }
    if (typ == "moneda") {
        if (auto *le = qobject_cast<QLineEdit*>(w)) {
            const QString norm = cleanNumericText(le->text());
            bool ok = false;
            const double d = norm.toDouble(&ok);
            return ok ? QVariant(d) : QVariant();
        }
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
            else if (auto *de  = qobject_cast<QDateEdit*>(w)) {
                de->setDate(de->minimumDate());
                de->setProperty("touched", false);
            }
            else if (auto *ds  = qobject_cast<QDoubleSpinBox*>(w)) ds->setValue(0.0);
        }
    }
}

void RecordsPage::addNewRowEditors(qint64 /*presetId*/)
{
    if (m_schema.isEmpty()) return;

    const int newRow = ui->twRegistros->rowCount();
    ui->twRegistros->insertRow(newRow);

    int idCol = -1;
    for (int c = 0; c < m_schema.size(); ++c) {
        if (normType(m_schema[c].type) == "autonumeracion") { idCol = c; break; }
    }

    if (idCol >= 0) {
        auto *it = new QTableWidgetItem;
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        it->setText("(New)");
        it->setForeground(QColor("#777"));
        ui->twRegistros->setItem(newRow, idCol, it);
    }

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
    if (m_isCommitting || m_isReloading) return;

    m_isCommitting = true;

    const int last   = ui->twRegistros->rowCount() - 1;
    const int rowIns = m_preparedNextNew ? (last - 1) : last;
    if (rowIns < 0) { m_isCommitting = false; return; }

    Record r; r.resize(m_schema.size());
    for (int c = 0; c < m_schema.size(); ++c) {
        const FieldDef& fd = m_schema[c];
        const QString t = normType(fd.type);

        if (t == "autonumeracion") {
            r[c] = QVariant();
        } else {
            QWidget* w = ui->twRegistros->cellWidget(rowIns, c);
            r[c] = editorValue(w, fd.type);

            if (normType(fd.type) == "texto" && fd.size > 0) {
                QString s = r[c].toString();
                if (s.size() > fd.size) {
                    const QString cut = s.left(fd.size);
                    r[c] = cut;

                    const QModelIndex idx = ui->twRegistros->model()->index(rowIns, c);
                    const QRect rect = ui->twRegistros->visualRect(idx);
                    QToolTip::showText(
                        ui->twRegistros->viewport()->mapToGlobal(rect.bottomRight()),
                        tr("Solo se aceptan %1 caracteres para \"%2\".\nSe recortó el texto.")
                            .arg(fd.size).arg(fd.name),
                        ui->twRegistros, rect, 2500);

                    if (auto *le = qobject_cast<QLineEdit*>(w)) {
                        le->setText(cut);
                        le->setCursorPosition(cut.size());
                    }
                }
            }
        }
    }

    QString err;
    if (!DataModel::instance().insertRow(m_tableName, r, &err)) {
        QMessageBox::warning(this, tr("No se pudo insertar"), err);
        m_isCommitting = false;
        return;
    }

    m_preparedNextNew = false;
    reloadRows();
    const int rows = ui->twRegistros->rowCount();
    if (rows >= 2) ui->twRegistros->setCurrentCell(rows - 2, 0);

    m_isCommitting = false;
}

/* =================== Helpers de estado/UI =================== */

void RecordsPage::setMode(RecordsPage::Mode m)
{
    m_mode = m;
    updateHeaderButtons();
}

void RecordsPage::updateHeaderButtons()
{
    // Toolbar oculta en esta vista
}

void RecordsPage::updateStatusLabels()
{
    if (auto *lbl = findChild<QLabel*>("lblTotal"))  lbl->clear();
    if (auto *lbl = findChild<QLabel*>("lblPagina")) lbl->clear();
}

/* =================== Acciones de encabezado =================== */

void RecordsPage::onTablaChanged(int /*index*/)
{
    const QString sel = ui->cbTabla->currentText().trimmed();
    const auto tables = DataModel::instance().tables();

    if (sel.isEmpty() || !tables.contains(sel)) {
        if (m_rowsConn) { disconnect(m_rowsConn); m_rowsConn = QMetaObject::Connection(); }
        m_tableName.clear();
        m_schema.clear();
        ui->twRegistros->clear();
        ui->twRegistros->setRowCount(0);
        ui->twRegistros->setColumnCount(0);
        ui->leBuscar->clear();
        setMode(RecordsPage::Mode::Idle);
        updateStatusLabels();
        updateNavState();
        return;
    }

    if (sel != m_tableName) {
        setTableFromFieldDefs(sel, DataModel::instance().schema(sel));
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

bool RecordsPage::editRecordDialog(const QString& title, const Schema& s, Record& r, bool, QString* errMsg)
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
            auto *de = new QDateEdit;
            de->setCalendarPopup(true);
            const QString base = baseFormatKey(s[i].formato);
            de->setDisplayFormat(base.isEmpty() ? "dd/MM/yy" : base);
            de->setLocale(QLocale(QLocale::Spanish, QLocale::Honduras));
            de->setSpecialValueText("");
            de->setMinimumDate(QDate(100,1,1));
            de->setDate(QDate::currentDate());
            if (r[i].canConvert<QDate>() && r[i].toDate().isValid())
                de->setDate(r[i].toDate());
            w = de;
        } else if (t == "booleano") {
            auto *cb = new QCheckBox("Sí"); cb->setChecked(r[i].toBool()); w = cb;
        } else if (t == "texto_largo") {
            auto *pt = new QPlainTextEdit; pt->setPlainText(r[i].toString()); w = pt;
        } else {
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

const Schema& RecordsPage::schema() const { return m_schema; }
QTableWidget* RecordsPage::sheet()  const { return ui->twRegistros; }

/* =================== “Datasheet mínimo”: ocultar controles legacy =================== */
void RecordsPage::hideLegacyChrome()
{
    auto hideByObjectName = [this](const char* name){
        if (auto *w = this->findChild<QWidget*>(name)) w->hide();
    };

    hideByObjectName("leBuscar");
    hideByObjectName("btnLimpiarBusqueda");

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

    hideByObjectName("btnInsertar");
    hideByObjectName("btnEditar");
    hideByObjectName("btnEliminar");
    hideByObjectName("btnGuardar");
    hideByObjectName("btnCancelar");
    for (auto *cb : findChildren<QCheckBox*>()) {
        const auto txt = cb->text().toLower();
        if (txt.contains("eliminad")) cb->hide();
    }

    hideByObjectName("btnPrimero");
    hideByObjectName("btnAnterior");
    hideByObjectName("btnSiguiente");
    hideByObjectName("btnUltimo");
    hideByObjectName("lblPagina");
    hideByObjectName("lblTotal");
    hideByObjectName("lblStatus");
    hideByObjectName("lblEstado");

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
            const FieldDef& fdf = m_schema[c];
            const QString base = baseFormatKey(fdf.formato);
            QLocale es(QLocale::Spanish, QLocale::Honduras);

            QDate d;
            if (base == "dd-MM-yy")      d = QDate::fromString(txt, "dd-MM-yy");
            else if (base == "dd/MM/yy") d = QDate::fromString(txt, "dd/MM/yy");
            else if (base == "dd/MMMM/yyyy")
                d = es.toDate(txt, "dd/MMMM/yyyy");

            if (!d.isValid())            d = QDate::fromString(txt, Qt::ISODate);

            rec[c] = d.isValid() ? QVariant(d) : QVariant();
            continue;

        } else if (t == "moneda" || t == "numero") {
            const QString norm = cleanNumericText(txt);
            bool ok = false;
            double d = norm.toDouble(&ok);
            if (ok) {
                rec[c] = d;
            } else {
                const auto &rows = DataModel::instance().rows(m_tableName);
                if (row >= 0 && row < rows.size() && c < rows[row].size())
                    rec[c] = rows[row][c];
                else
                    rec[c] = QVariant();
            }
        } else if (t == "booleano") {
            rec[c] = false;
        } else if (t == "autonumeracion") {
            rec[c] = txt;
        } else {
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

    m_isCommitting = true;
    QSignalBlocker guard(ui->twRegistros);

    Record r = rowToRecord(row);

    const FieldDef& fd = m_schema[col];
    bool overflowed = false;
    QString cutText;

    if (normType(fd.type) == "texto" && fd.size > 0) {
        const QString current = it->text();
        if (current.size() > fd.size) {
            overflowed = true;
            cutText = current.left(fd.size);
            r[col] = cutText;
        }
    }

    QString err;
    if (!DataModel::instance().updateRow(m_tableName, row, r, &err)) {
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
        if (overflowed) {
            if (auto *cell = ui->twRegistros->item(row, col))
                cell->setText(cutText);

            const QRect rect = ui->twRegistros->visualItemRect(it);
            QToolTip::showText(
                ui->twRegistros->viewport()->mapToGlobal(rect.bottomRight()),
                tr("Solo se aceptan %1 caracteres para \"%2\".\nSe recortó el texto.")
                    .arg(fd.size).arg(fd.name),
                ui->twRegistros, rect, 2500);
        }
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

    const int last = ui->twRegistros->rowCount() - 1;
    bool hasData = false;
    for (int c = 0; c < ui->twRegistros->columnCount(); ++c) {
        if (QWidget *w = ui->twRegistros->cellWidget(last, c)) {
            QString t;
            if (auto *le = qobject_cast<QLineEdit*>(w))        t = le->text().trimmed();
            else if (auto *de = qobject_cast<QDateEdit*>(w)) {
                const bool touched = de->property("touched").toBool();
                t = (touched && de->date() != de->minimumDate()) ? "x" : "";
            }
            else if (auto *cb = qobject_cast<QCheckBox*>(w))   t = cb->isChecked() ? "1" : "";
            if (!t.isEmpty() && t != "0" && t != "0.0" && t != "0.00") { hasData = true; break; }
        }
    }
    if (!hasData) return;

    const int curRow = ui->twRegistros->currentRow();
    const int curCol = ui->twRegistros->currentColumn();

    QSignalBlocker block(ui->twRegistros);
    addNewRowEditors(-1);

    if (curRow >= 0 && curCol >= 0) {
        ui->twRegistros->setCurrentCell(curRow, curCol);
        if (QWidget *ed = ui->twRegistros->cellWidget(curRow, curCol))
            ed->setFocus();
    }

    m_preparedNextNew = true;
}

void RecordsPage::onCurrentCellChanged(int currentRow, int, int previousRow, int)
{
    if (m_isReloading || m_isCommitting) return;
    const int last = ui->twRegistros->rowCount() - 1;

    if (currentRow == last) {
        m_preparedNextNew = false;
    }

    if (previousRow >= 0 && currentRow >= 0
        && currentRow != previousRow
        && previousRow == last)
    {
        if (!m_preparedNextNew) return;
        commitNewRow();
    }
}
