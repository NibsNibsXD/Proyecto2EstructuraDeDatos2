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
#include <QToolTip>
#include <QCursor>
#include <QRegularExpression>
#include <QDoubleValidator>
#include <QRegularExpressionValidator>
#include <QToolTip>
#include <QKeyEvent>
#include <QClipboard>
#include <QKeyEvent>
#include <QKeySequence>
#include <QClipboard>
#include <QGuiApplication>
#include <QFocusEvent>
#include <QMouseEvent>



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

        // Validaciones por tecla
        if (ev->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent*>(ev);
            const int k = ke->key();

            // Pegado por atajo (⌘V / Ctrl+V / Shift+Insert)
            if (ke->matches(QKeySequence::Paste) ||
                (ke->modifiers() == Qt::ShiftModifier && ke->key() == Qt::Key_Insert)) {
                const QString clip = QGuiApplication::clipboard()->text();
                if (!accepts(clip)) {
                    showCellTip(m_owner, m_idx,
                                m_isInt ? QObject::tr("Solo enteros (sin decimales).")
                                        : QObject::tr("Solo números con hasta %1 decimales.").arg(m_dp));
                    return true; // bloquear
                }
                insertNormalized(clip);
                return true; // ya procesamos el pegado
            }


            // Deja pasar navegación/edición
            if (ke->modifiers() & (Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier)) return false;
            if (k==Qt::Key_Backspace || k==Qt::Key_Delete ||
                k==Qt::Key_Left || k==Qt::Key_Right ||
                k==Qt::Key_Home || k==Qt::Key_End || k==Qt::Key_Tab || k==Qt::Key_Return || k==Qt::Key_Enter)
                return false;

            const QString t = ke->text();
            if (t.isEmpty()) return false;

            // Letras no permitidas
            if (t.contains(QRegularExpression("[A-Za-z]"))) {
                showCellTip(m_owner, m_idx, QObject::tr("Solo números."));
                return true; // bloquear
            }

            // Signo menos: solo una vez y al principio
            if (t == "-") {
                if (m_le->cursorPosition()!=0 || m_le->text().contains('-')) {
                    showCellTip(m_owner, m_idx, QObject::tr("El signo '-' solo al inicio."));
                    return true;
                }
                return false;
            }

            // Punto/coma decimal
            if (t == "." || t == ",") {
                if (m_isInt) {
                    showCellTip(m_owner, m_idx, QObject::tr("Este campo es entero; no admite decimales."));
                    return true;
                }
                // Normaliza a punto
                insertNormalized(".");
                return true; // ya insertamos
            }

            // Dígitos
            if (t.contains(QRegularExpression("\\d"))) {
                // ¿respetará el límite de decimales?
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
                return false; // dejar pasar
            }

            // Cualquier otro símbolo: bloquear
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
        // construye el resultado potencial y revisa reglas
        QString in = chunk;
        in.replace(',', '.');
        QString next = prospectiveText(in);

        // letras -> no
        if (next.contains(QRegularExpression("[A-Za-z]"))) return false;

        // enteros: no punto
        if (m_isInt && next.contains('.')) return false;

        // decimales: respeta dp
        if (!m_isInt) {
            int dot = next.indexOf('.');
            if (dot >= 0) {
                int after = next.mid(dot+1).remove(QRegularExpression("[^0-9]")).size();
                if (after > m_dp) return false;
            }
        }
        // signo '-' válido solo al inicio y una vez
        int minusCount = next.count('-');
        if (minusCount > 1) return false;
        if (minusCount==1 && !next.startsWith('-')) return false;

        // el validador final aún hará chequeo numérico, esto es UX inmediato
        return true;
    }

    void insertNormalized(QString s) {
        s.replace(',', '.');
        // emular tecleo: reemplaza selección o inserta en cursor
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


// --- Delegado: editor encaja en la celda, ajusta altura y aplica reglas por tipo
class DatasheetDelegate : public QStyledItemDelegate {
public:
    explicit DatasheetDelegate(RecordsPage* owner)
        : QStyledItemDelegate(owner), m_owner(owner) {}

    QWidget* createEditor(QWidget *parent,
                          const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const override
    {
        QLineEdit *le = new QLineEdit(parent);
        le->setFrame(false);
#ifdef Q_OS_MAC
        le->setAttribute(Qt::WA_MacShowFocusRect, false);
#endif
        le->setStyleSheet("margin:0; padding:2px 4px;");

        // --- Reglas por columna (según Schema)
        if (m_owner && idx.isValid()) {
            const Schema& s = m_owner->schema();
            if (idx.column() >= 0 && idx.column() < s.size()) {
                const FieldDef& fd = s[idx.column()];
                const QString t = normType(fd.type);

                // 1) Texto / Texto largo: limitar longitud + tooltip al topar
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

                // 2) Número: validador (bloquea letras desde la primera tecla)
                if (t == "numero") {
                    const QString sz = fd.autoSubtipo.trimmed().toLower();
                    const bool isInt = sz.contains("byte") || sz.contains("entero")
                                       || sz.contains("integer") || sz.contains("long");
                    if (isInt) {
                        // Entero 64-bit aprox: permite vacío mientras se edita
                        auto *rxv = new QRegularExpressionValidator(
                            QRegularExpression(QStringLiteral("^-?\\d{0,19}$")), le);
                        le->setValidator(rxv);
                        le->setPlaceholderText(QObject::tr("Solo enteros"));
                        auto *f = new NumInputFilter(le, m_owner, idx, /*isInt=*/true, /*dp=*/0, le);
                        le->installEventFilter(f);
                    } else {
                        const int dp = std::clamp(parseDecPlaces(fd.formato), 0, 4);
                        auto *dv = new QDoubleValidator(le);
                        dv->setDecimals(dp);
                        dv->setNotation(QDoubleValidator::StandardNotation);
                        dv->setRange(-1e12, 1e12); // ajusta si lo deseas
                        le->setValidator(dv);
                        le->setPlaceholderText(
                            QObject::tr("Número con hasta %1 decimales").arg(dp));
                        // <<< instala filtro (usa dp)
                        auto *f = new NumInputFilter(le, m_owner, idx, /*isInt=*/false, dp, le);
                        le->installEventFilter(f);
                    }
                }

                // 2-bis) Moneda: validador igual que número (con dp del formato)
                if (t == "moneda") {
                    const int dp = std::clamp(parseDecPlaces(fd.formato), 0, 4);
                    auto *dv = new QDoubleValidator(le);
                    dv->setDecimals(dp);
                    dv->setNotation(QDoubleValidator::StandardNotation);
                    dv->setRange(-1e12, 1e12);
                    le->setValidator(dv);
                    le->setPlaceholderText(
                        QObject::tr("Monto con hasta %1 decimales").arg(dp));

                    auto *f = new NumInputFilter(le, m_owner, idx, /*isInt=*/false, dp, le);
                    le->installEventFilter(f);
                }

                if (t == "fecha_hora") {
                    auto *de = new QDateEdit(parent);

                    // Formato + localización
                    const QString base = baseFormatKey(fd.formato);  // "dd-MM-yy" | "dd/MM/yy" | "dd/MMMM/yyyy"
                    de->setDisplayFormat(base.isEmpty() ? "dd/MM/yy" : base);
                    de->setLocale(QLocale(QLocale::Spanish, QLocale::Honduras));
                    de->setCalendarPopup(true);
                    de->setFrame(false);
                    de->setKeyboardTracking(false);
                    de->setFocusPolicy(Qt::StrongFocus);

                    // Visual: no “01/01/00”, usa HOY en filas existentes
                    de->setSpecialValueText("");
                    de->setMinimumDate(QDate(100, 1, 1));
                    de->setDate(QDate::currentDate());

                    // touched solo una vez (el bloque estaba duplicado)
                    de->setProperty("touched", false);
                    QObject::connect(de, &QDateEdit::dateChanged, de, [de]{
                        de->setProperty("touched", true);
                    });

                    // Abrir popup al foco/click
                    de->installEventFilter(new DatePopupOpener(de));
                    return de;
                }


            }
        }

        // Ajuste de altura de fila para que no desborde
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
                // si estaba vacío/ilegible, usa HOY para no ver 01/01/00
                de->setDate(QDate::currentDate());
                de->setProperty("touched", true); // cuenta como dato si el usuario no toca más
            }
            de->blockSignals(false);
            return;

        }


        // 2) Número/Moneda: abrir editor con texto "limpio" (sin símbolo ni miles)
        if (auto *le = qobject_cast<QLineEdit*>(editor)) {
            if (!m_owner || !idx.isValid()) {
                QStyledItemDelegate::setEditorData(editor, idx);
                return;
            }
            const FieldDef& fd = m_owner->schema()[idx.column()];
            const QString t = normType(fd.type);
            if (t == "numero" || t == "moneda") {
                // Limpia símbolo (L, $), miles, NBSP, etc.
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
        QAbstractItemView::CurrentChanged      // ← inicia edición al cambiar de celda
        | QAbstractItemView::SelectedClicked
        | QAbstractItemView::DoubleClicked
        | QAbstractItemView::EditKeyPressed);
    ui->twRegistros->setAlternatingRowColors(true);
    ui->twRegistros->setSortingEnabled(false);
    ui->twRegistros->verticalHeader()->setVisible(false);

    auto *hh = ui->twRegistros->horizontalHeader();
    // Editor inline bien encajado y scroll horizontal para texto largo
    ui->twRegistros->setItemDelegate(new DatasheetDelegate(this));
    ui->twRegistros->setWordWrap(false);
    ui->twRegistros->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->twRegistros->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    // (opcional útil) doble-click en encabezado => auto-ajustar columna
    auto *hh2 = ui->twRegistros->horizontalHeader();
    connect(hh2, &QHeaderView::sectionDoubleClicked, this,
            [this](int c){ ui->twRegistros->resizeColumnToContents(c); });
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

    // Reconstruir combo completo desde el modelo (evita nombres huérfanos)
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

    // Refrescar al vuelo si cambian filas en el modelo
    m_rowsConn = connect(&DataModel::instance(), &DataModel::rowsChanged, this,
                         [this](const QString& t){ if (t == m_tableName) reloadRows(); });

    setMode(Mode::Idle);
    updateStatusLabels();
    updateNavState();

    // --- Mantener Datasheet en sync con DataModel ---
    auto& dm = DataModel::instance();

    connect(&dm, &DataModel::tableCreated, this, [this](const QString& t) {
        // Si aparece una tabla nueva, aseguramos verla en el combo si no está
        if (ui->cbTabla->findText(t) < 0) ui->cbTabla->addItem(t);
    });

    connect(&dm, &DataModel::tableDropped, this, [this](const QString& t) {
        // La quitamos del combo
        int idx = ui->cbTabla->findText(t);
        if (idx >= 0) ui->cbTabla->removeItem(idx);

        // Si era la que estaba abierta, limpiamos TODO el datasheet
        if (t == m_tableName) {
            if (m_rowsConn) { disconnect(m_rowsConn); m_rowsConn = QMetaObject::Connection(); }
            m_tableName.clear();
            m_schema.clear();
            ui->twRegistros->clear();
            ui->twRegistros->setRowCount(0);
            ui->twRegistros->setColumnCount(0);
            ui->leBuscar->clear();
            setMode(Mode::Idle);
            updateStatusLabels();
            updateNavState();
        }
    });

    connect(&dm, &DataModel::schemaChanged, this, [this](const QString& t, const Schema& s) {
        // Si cambia el esquema de la tabla actual, se reconstruyen columnas y filas
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

        const QString base = baseFormatKey(fd.formato); // "dd-MM-yy" | "dd/MM/yy" | "dd/MMMM/yyyy"
        if (base == "dd-MM-yy")  return d.toString("dd-MM-yy");
        if (base == "dd/MM/yy")  return d.toString("dd/MM/yy");

        // dd/MESTEXTO/YYYY -> "dd/MMMM/yyyy" con mes en español
        QLocale es(QLocale::Spanish, QLocale::Honduras);
        return es.toString(d, "dd/MMMM/yyyy"); // p.ej. 05/enero/2025
    }


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
        // ⬇️ 1) si el base está vacío (acabas de cambiar a "Moneda"), usa "LPS" por defecto
        QString base = baseFormatKey(fd.formato).toUpper();
        if (base.isEmpty()) base = "LPS";

        // ⬇️ 2) símbolos, incluyendo “M” para Millares
        QString sym;
        if (base.startsWith("LPS") || base.startsWith("HNL") || base.startsWith("L ")) sym = "L ";
        else if (base.startsWith("USD") || base.startsWith("$"))                      sym = "$";
        else if (base.startsWith("EUR") || base.startsWith("€"))                      sym = "€";
        else if (base.startsWith("MILLARES") || base.startsWith("M"))
            sym = "M ";

        const QLocale loc = QLocale::system();
        return sym + loc.toString(v.toDouble(), 'f', dp);
    }

    // Fallback seguro para texto/autonumeración/otros
    return v.toString();
}


void RecordsPage::reloadRows()
{
    if (m_tableName.isEmpty() || m_schema.isEmpty()) return;

    const auto tables = DataModel::instance().tables();
    if (!tables.contains(m_tableName)) {
        // La tabla ya no existe → limpiar y salir
        m_schema.clear();
        ui->twRegistros->clear();
        ui->twRegistros->setRowCount(0);
        ui->twRegistros->setColumnCount(0);
        setMode(Mode::Idle);
        updateStatusLabels();
        updateNavState();
        return;
    }

    m_isReloading = true;

    auto oldTriggers = ui->twRegistros->editTriggers();
    ui->twRegistros->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 1) Cerrar TODOS los editores vivos y cellWidgets antes de reconstruir,
    //    sin dejar residuos que se dibujen encima (0,0)
    ui->twRegistros->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // a) elimina todos los editores temporales (qt_editing_widget)
    const auto editors = ui->twRegistros->viewport()->findChildren<QWidget*>(
        "qt_editing_widget", Qt::FindChildrenRecursively);
    for (QWidget *ed : editors) {
        ed->blockSignals(true);
        ed->hide();
        ed->deleteLater();
    }

    // b) elimina cualquier cellWidget que hubiera en TODA la tabla
    for (int r = 0; r < ui->twRegistros->rowCount(); ++r) {
        for (int c = 0; c < ui->twRegistros->columnCount(); ++c) {
            if (QWidget *w = ui->twRegistros->cellWidget(r, c)) {
                ui->twRegistros->removeCellWidget(r, c);
                w->deleteLater();
            }
        }
    }

    // c) reinstala SIEMPRE tu delegado bueno (NO el genérico)
    ui->twRegistros->setItemDelegate(new DatasheetDelegate(this));



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
                    // ver el contenido completo al pasar el mouse
                    it->setToolTip(formatCell(fd, vv));

                    ui->twRegistros->setItem(r, c, it);
                }
            }
        }

        // 5) Triggers de edición seguros (NO AllEditTriggers)
        ui->twRegistros->setEditTriggers(
            QAbstractItemView::CurrentChanged
            | QAbstractItemView::SelectedClicked
            | QAbstractItemView::DoubleClicked
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

    // Defensa final: si algún editor quedó vivo por eventos diferidos, elimínalo sin señales
    QTimer::singleShot(0, this, [this]{
        const auto editors2 = ui->twRegistros->viewport()->findChildren<QWidget*>(
            "qt_editing_widget", Qt::FindChildrenRecursively);
        for (QWidget *ed : editors2) {
            ed->blockSignals(true);
            ed->hide();
            ed->deleteLater();
        }
    });

    // Restaura los triggers de edición originales
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
        de->setDate(QDate::currentDate());     // (New) muestra HOY, nada de 01/01/00
        de->setKeyboardTracking(false);
        de->setFocusPolicy(Qt::StrongFocus);

        // flags + disparos
        de->setProperty("touched", false);
        connect(de, &QDateEdit::dateChanged, de, [de]{ de->setProperty("touched", true); });

        // 1) Prepara la siguiente (New) cuando cambia la fecha
        connect(de, &QDateEdit::dateChanged, this, &RecordsPage::prepareNextNewRow);
        // 2) Y TAMBIÉN cuando cierra el editor (por si dejó la misma fecha de hoy)
        connect(de, &QDateEdit::editingFinished, this, &RecordsPage::prepareNextNewRow);

        return de;
    }



    // recordspage.cpp (editor para 'moneda' en fila New) — usar QLineEdit
    if (t == "moneda") {
        auto *le = new QLineEdit;
        const int dp = std::clamp(parseDecPlaces(fd.formato), 0, 4);
        auto *dv = new QDoubleValidator(le);
        dv->setDecimals(dp);
        dv->setNotation(QDoubleValidator::StandardNotation);
        dv->setRange(-1e12, 1e12);
        le->setValidator(dv);
        le->setPlaceholderText(tr("Monto con hasta %1 decimales").arg(dp));

        // mismo filtro que “Número” para puntos/comas y límite de decimales
        auto *f = new NumInputFilter(le, const_cast<RecordsPage*>(this),
                                     QModelIndex(), /*isInt=*/false, dp, le);
        le->installEventFilter(f);

        // dispara preparación/commit SOLO cuando el usuario edita, no al enfocarlo
        connect(le, &QLineEdit::textEdited,     this, &RecordsPage::prepareNextNewRow);
        connect(le, &QLineEdit::editingFinished,this, &RecordsPage::commitNewRow);
        return le;
    }


    if (t == "booleano") {
        auto *cb = new QCheckBox;
        cb->setTristate(false);
        connect(cb, &QCheckBox::checkStateChanged, this, &RecordsPage::prepareNextNewRow);
        connect(cb, &QCheckBox::checkStateChanged, this, &RecordsPage::commitNewRow);
        // lo devolvemos tal cual; abajo soportamos tanto wrap como directo
        return cb;
    }

    if (t == "numero") {
        auto *le = new QLineEdit;

        const bool isInt = fd.autoSubtipo.trimmed().toLower().contains("byte")
                           || fd.autoSubtipo.trimmed().toLower().contains("entero")
                           || fd.autoSubtipo.trimmed().toLower().contains("integer")
                           || fd.autoSubtipo.trimmed().toLower().contains("long");

        if (isInt) {
            // Solo enteros (permite vacío mientras editas)
            le->setValidator(new QRegularExpressionValidator(
                QRegularExpression(QStringLiteral("^-?\\d{0,19}$")), le));
            le->setPlaceholderText(tr("Solo enteros"));
        } else {
            const int dp = std::clamp(parseDecPlaces(fd.formato), 0, 4);
            auto *dv = new QDoubleValidator(le);
            dv->setDecimals(dp);
            dv->setNotation(QDoubleValidator::StandardNotation);
            dv->setRange(-1e12, 1e12); // ajusta si quieres
            le->setValidator(dv);
            le->setPlaceholderText(tr("Número con hasta %1 decimales").arg(dp));
        }

        connect(le, &QLineEdit::textEdited, this, &RecordsPage::prepareNextNewRow);
        connect(le, &QLineEdit::editingFinished, this, &RecordsPage::commitNewRow);

        {
            const bool isInt = fd.autoSubtipo.trimmed().toLower().contains("byte")
            || fd.autoSubtipo.trimmed().toLower().contains("entero")
                || fd.autoSubtipo.trimmed().toLower().contains("integer")
                || fd.autoSubtipo.trimmed().toLower().contains("long");
            const int dp = std::clamp(parseDecPlaces(fd.formato), 0, 4);
            auto *f = new NumInputFilter(le, const_cast<RecordsPage*>(this),
                                         QModelIndex(), isInt, isInt ? 0 : dp, le);
            le->installEventFilter(f);
        }
        return le;
    }


    if (t == "texto_largo") {
        auto *le = new QLineEdit; // inline simple

        if (fd.size > 0) {
            // Deja escribir normal y avisa + recorta al exceder
            connect(le, &QLineEdit::textEdited, this,
                    [this, le, max = fd.size, field = fd.name](const QString& s) {
                        if (s.size() > max) {
                            const QString cut = s.left(max);
                            QSignalBlocker b(le);
                            le->setText(cut);
                            le->setCursorPosition(cut.size());

                            // Tooltip pegado al editor (fila New)
                            QToolTip::showText(
                                le->mapToGlobal(QPoint(le->width(), le->height())),
                                tr("Solo se aceptan %1 caracteres para \"%2\".\nSe recortó el texto.")
                                    .arg(max).arg(field),
                                le, le->rect(), 2000);
                        }
                    });
        }

        connect(le, &QLineEdit::textEdited, this, &RecordsPage::prepareNextNewRow);
        connect(le, &QLineEdit::returnPressed, this, &RecordsPage::commitNewRow);
        return le;
    }

    // texto corto / número
    auto *le = new QLineEdit;

    // AVISO EN VIVO para Short Text (normType == "texto")
    if (normType(fd.type) == "texto" && fd.size > 0) {
        connect(le, &QLineEdit::textEdited, this,
                [this, le, max = fd.size, field = fd.name](const QString& s) {
                    if (s.size() > max) {
                        const QString cut = s.left(max);
                        QSignalBlocker b(le);
                        le->setText(cut);
                        le->setCursorPosition(cut.size());

                        // Tooltip pegado al editor
                        QToolTip::showText(
                            le->mapToGlobal(QPoint(le->width(), le->height())),
                            tr("Solo se aceptan %1 caracteres para \"%2\".\nSe recortó el texto.")
                                .arg(max).arg(field),
                            le, le->rect(), 2000);
                    }
                });
    }

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

    // --- Fecha ---
    if (typ == "fecha_hora") {
        if (auto *de = qobject_cast<QDateEdit*>(w)) {
            const bool touched = de->property("touched").toBool();
            // Si no tocó el control y la celda original estaba vacía, no guardes nada
            // (RecordsPage usa este return para construir el QVariant a insertar)
            if (!touched) {
                return QVariant();  // deja NULL si no eligió
            }
            return QVariant(de->date());
        }
        if (auto *le = qobject_cast<QLineEdit*>(w)) {
            // Respaldo: parsear por los 3 formatos
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
            // normaliza el texto y conviértelo a double
            const QString norm = cleanNumericText(le->text());
            bool ok = false;
            const double d = norm.toDouble(&ok);
            return ok ? QVariant(d) : QVariant();
        }
        if (auto *ds = qobject_cast<QDoubleSpinBox*>(w)) {
            return ds->value(); // por si quedara algún spinbox legacy
        }
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
                de->setDate(de->minimumDate());       // queda visualmente vacío
                de->setProperty("touched", false);    // aún no eligió
            }
            else if (auto *ds  = qobject_cast<QDoubleSpinBox*>(w)) ds->setValue(0.0);
        }
    }
}


// void RecordsPage::addNewRowEditors(qint64 presetId)
// {
//     if (m_schema.isEmpty()) return;

//     const int newRow = ui->twRegistros->rowCount();
//     ui->twRegistros->insertRow(newRow);

//     // localizar la primera columna autonumeración (ID)
//     int idCol = -1;
//     for (int c = 0; c < m_schema.size(); ++c) {
//         if (normType(m_schema[c].type) == "autonumeracion") { idCol = c; break; }
//     }

//     // Preasigna el ID real y deja la celda NO editable (sin cellWidget)
//     if (idCol >= 0) {
//         qint64 idVal;
//         if (presetId >= 0) {
//             idVal = presetId;
//         } else {
//             // Primera (New) tras recargar: consulta al modelo una sola vez
//             idVal = DataModel::instance().nextAutoNumber(m_tableName).toLongLong();
//         }
//         auto *it = new QTableWidgetItem;
//         it->setData(Qt::DisplayRole, static_cast<qlonglong>(idVal)); // pinta como número, no texto
//         it->setFlags(it->flags() & ~Qt::ItemIsEditable);
//         ui->twRegistros->setItem(newRow, idCol, it);
//     }

//     // Resto de columnas: editores inline
//     for (int c = 0; c < m_schema.size(); ++c) {
//         if (c == idCol) continue;
//         const FieldDef& fd = m_schema[c];
//         QWidget* ed = makeEditorFor(fd);
//         ui->twRegistros->setCellWidget(newRow, c, ed);
//     }
// }

void RecordsPage::addNewRowEditors(qint64 /*presetId*/)
{
    if (m_schema.isEmpty()) return;

    const int newRow = ui->twRegistros->rowCount();
    ui->twRegistros->insertRow(newRow);

    // localizar la primera columna autonumeración (ID)
    int idCol = -1;
    for (int c = 0; c < m_schema.size(); ++c) {
        if (normType(m_schema[c].type) == "autonumeracion") { idCol = c; break; }
    }

    // PREASIGNA SOLO UN PLACEHOLDER VISUAL, SIN NÚMERO REAL
    if (idCol >= 0) {
        auto *it = new QTableWidgetItem;
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        it->setText("(New)");                 // ← solo visual
        it->setForeground(QColor("#777"));
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
            // Deja que DataModel::insertRow() asigne el ID atómicamente
            r[c] = QVariant();   // nulo => el modelo genera el siguiente ID


        } else {
            QWidget* w = ui->twRegistros->cellWidget(rowIns, c);
            r[c] = editorValue(w, fd.type);
            // --- Aviso si Short Text supera el límite en (New) ---
            if (normType(fd.type) == "texto" && fd.size > 0) {
                QString s = r[c].toString();
                if (s.size() > fd.size) {
                    QString cut = s.left(fd.size);
                    r[c] = cut;

                    // Mostrar aviso cerca de la celda
                    const QModelIndex idx = ui->twRegistros->model()->index(rowIns, c);
                    const QRect rect = ui->twRegistros->visualRect(idx);
                    QToolTip::showText(
                        ui->twRegistros->viewport()->mapToGlobal(rect.bottomRight()),
                        tr("Solo se aceptan %1 caracteres para \"%2\".\nSe recortó el texto.")
                            .arg(fd.size).arg(fd.name),
                        ui->twRegistros, rect, 2500);

                    // Refleja el recorte en el editor si es QLineEdit
                    if (QWidget *w2 = ui->twRegistros->cellWidget(rowIns, c)) {
                        if (auto *le = qobject_cast<QLineEdit*>(w2)) {
                            le->setText(cut);
                            le->setCursorPosition(cut.size());
                        }
                    }
                }
            }

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
    const QString sel = ui->cbTabla->currentText().trimmed();
    const auto tables = DataModel::instance().tables();

    // Si no hay selección o la tabla ya no existe → limpiar todo
    if (sel.isEmpty() || !tables.contains(sel)) {
        if (m_rowsConn) { disconnect(m_rowsConn); m_rowsConn = QMetaObject::Connection(); }
        m_tableName.clear();
        m_schema.clear();
        ui->twRegistros->clear();
        ui->twRegistros->setRowCount(0);
        ui->twRegistros->setColumnCount(0);
        ui->leBuscar->clear();
        setMode(Mode::Idle);
        updateStatusLabels();
        updateNavState();
        return;
    }

    // Si el usuario eligió otra tabla válida → cargarla
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
            auto *de = new QDateEdit;
            de->setCalendarPopup(true);

            // Formato según schema
            const QString base = baseFormatKey(s[i].formato); // "dd-MM-yy" | "dd/MM/yy" | "dd-MMMM-yyyy"
            de->setDisplayFormat(base.isEmpty() ? "dd/MM/yy" : base);

            // Español para "MMMM"
            de->setLocale(QLocale(QLocale::Spanish, QLocale::Honduras));

            // NACE VACÍO: usar specialValueText para mostrar vacío
            de->setSpecialValueText("");
            de->setMinimumDate(QDate(100,1,1));
            de->setDate(QDate::currentDate());

            // Si el registro traía un valor válido, úsalo
            if (r[i].canConvert<QDate>() && r[i].toDate().isValid())
                de->setDate(r[i].toDate());

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

const Schema& RecordsPage::schema() const { return m_schema; }
QTableWidget* RecordsPage::sheet()  const { return ui->twRegistros; }


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
            const FieldDef& fdf = m_schema[c];
            const QString base = baseFormatKey(fdf.formato);   // "dd-MM-yy" | "dd/MM/yy" | "dd/MMMM/yyyy"
            QLocale es(QLocale::Spanish, QLocale::Honduras);

            QDate d;
            if (base == "dd-MM-yy")      d = QDate::fromString(txt, "dd-MM-yy");
            else if (base == "dd/MM/yy") d = QDate::fromString(txt, "dd/MM/yy");
            else if (base == "dd/MMMM/yyyy")
                d = es.toDate(txt, "dd/MMMM/yyyy");

            // Fallbacks por si en alguna parte quedó ISO
            if (!d.isValid())            d = QDate::fromString(txt, Qt::ISODate);   // "yyyy-MM-dd"

            rec[c] = d.isValid() ? QVariant(d) : QVariant();
            continue;



        } else if (t == "moneda" || t == "numero") {
            const QString norm = cleanNumericText(txt);
            bool ok = false;
            double d = norm.toDouble(&ok);
            if (ok) {
                rec[c] = d;
            } else {
                // ← Evita vaciar la celda: conserva el valor anterior del modelo
                const auto &rows = DataModel::instance().rows(m_tableName);
                if (row >= 0 && row < rows.size() && c < rows[row].size())
                    rec[c] = rows[row][c];
                else
                    rec[c] = QVariant(); // fallback
            }
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

    // --- Aviso si Short Text supera el límite ---
    const FieldDef& fd = m_schema[col];
    bool overflowed = false;
    QString cutText;

    if (normType(fd.type) == "texto" && fd.size > 0) {
        const QString current = it->text();
        if (current.size() > fd.size) {
            overflowed = true;
            cutText = current.left(fd.size);
            r[col] = cutText;  // Enviar ya recortado al modelo
        }
    }

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
        // ▼ AQUÍ va tu bloque
        if (overflowed) {
            // Reflejar en UI sin reentrar (ya hay QSignalBlocker guard)
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

// void RecordsPage::prepareNextNewRow()
// {
//     if (m_preparedNextNew) return;
//     if (ui->twRegistros->rowCount() <= 0) return;

//     // Guarda la celda activa
//     const int curRow = ui->twRegistros->currentRow();
//     const int curCol = ui->twRegistros->currentColumn();

//     // Última fila existente (la (New) actual)
//     const int prevRow = ui->twRegistros->rowCount() - 1;

//     // Encontrar columna ID
//     int idCol = -1;
//     for (int c = 0; c < m_schema.size(); ++c) {
//         if (normType(m_schema[c].type) == "autonumeracion") { idCol = c; break; }
//     }

//     // Calcular el ID de la nueva (New) como (ID de la (New) actual) + 1
//     qint64 nextIdPreset = -1;
//     if (idCol >= 0) {
//         if (auto *it = ui->twRegistros->item(prevRow, idCol)) {
//             bool ok = false;
//             const qint64 lastNewId = it->text().toLongLong(&ok);
//             if (ok) nextIdPreset = lastNewId + 1;
//         }
//     }

//     // Evitar señales durante la inserción
//     QSignalBlocker block(ui->twRegistros);

//     // Insertar nueva fila (New) con ID preasignado incremental
//     addNewRowEditors(nextIdPreset);

//     // Restaurar foco donde estaba el usuario
//     if (curRow >= 0 && curCol >= 0) {
//         ui->twRegistros->setCurrentCell(curRow, curCol);
//         if (QWidget *ed = ui->twRegistros->cellWidget(curRow, curCol))
//             ed->setFocus();
//     }

//     m_preparedNextNew = true;
// }

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
                // cuenta como dato SOLO si eligió algo
                const bool touched = de->property("touched").toBool();
                t = (touched && de->date() != de->minimumDate()) ? "x" : "";
            }
            else if (auto *cb = qobject_cast<QCheckBox*>(w))   t = cb->isChecked() ? "1" : "";
            // Evita contar "0", "0.0", "0.00" como "datos"
            if (!t.isEmpty() && t != "0" && t != "0.0" && t != "0.00") { hasData = true; break; }
        }
    }
    if (!hasData) return;

    // Guarda la celda activa
    const int curRow = ui->twRegistros->currentRow();
    const int curCol = ui->twRegistros->currentColumn();

    // Evitar señales durante la inserción de la nueva (New)
    QSignalBlocker block(ui->twRegistros);

    // Inserta la fila (New) SIN preasignar ID (el modelo lo generará al insertar)
    addNewRowEditors(-1);   // el parámetro se ignora en tu nueva implementación

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


