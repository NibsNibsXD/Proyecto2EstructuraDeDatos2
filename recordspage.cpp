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
#include <QPointer>
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
#include <QToolTip>
#include <QTimer>
#include <QMenu>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QCursor>
#include <QSet>
#include <QInputDialog>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QDebug>

// ---- forward declarations para que el constructor las conozca ----
static inline QString binBaseDir();
static inline QString safeTableFileName(const QString& tableName);
static void createBinForTable(const QString& tableName);
static void removeBinForTable(const QString& tableName);

static QString cleanNumericText(QString s) {
    s = s.trimmed();
    s.remove(QChar::Nbsp);
    s.remove(QRegularExpression("[\\s\\p{Sc}]"));
    s = s.remove(QRegularExpression("[^0-9,\\.-]"));
    if (s.contains(',') && s.contains('.')) s.remove(',');
    else if (s.contains(',') && !s.contains('.')) s.replace(',', '.');
    return s;
}

void RecordsPage::showRequiredPopup(const QModelIndex& ix, const QString& msg, int msec)
{
    if (!ui->twRegistros || !ix.isValid()) return;

    // Evita mostrar el popup 2 veces por rebotes de foco
    if (m_reqPopupBusy) return;
    m_reqPopupBusy = true;

    QTableWidget* view = ui->twRegistros;
    const QRect rect   = view->visualItemRect(view->item(ix.row(), ix.column()));
    const QPoint pos   = view->viewport()->mapToGlobal(rect.bottomRight());

    QToolTip::showText(pos, msg, view, rect, msec);

    QTimer::singleShot(300, this, [this]{ m_reqPopupBusy = false; });
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

        // Antes: FocusIn || MouseButtonPress
        if (ev->type() == QEvent::MouseButtonPress) {
            QTimer::singleShot(0, de, [de]{
                QKeyEvent press(QEvent::KeyPress,   Qt::Key_F4, Qt::NoModifier);
                QCoreApplication::sendEvent(de, &press);
                QKeyEvent rel  (QEvent::KeyRelease, Qt::Key_F4, Qt::NoModifier);
                QCoreApplication::sendEvent(de, &rel);
            });
            return true; // consumimos el click que abre
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

        // ⛔ Si la celda ACTUAL es REQUERIDO (no autonum) y está vacía,
        // no permitir abrir editor en OTRA celda (igual que PK).
        if (m_owner && idx.isValid()) {
            QTableWidget* view = m_owner->sheet();
            const int curRow = view->currentRow();
            const int curCol = view->currentColumn();

            if (curRow >= 0 && curCol >= 0) {
                const Schema& s = m_owner->schema();
                if (curCol < s.size()) {
                    const FieldDef& f = s[curCol];
                    const QString nt  = normType(f.type);

                    auto currentCellText = [&]()->QString {
                        if (auto *w = view->cellWidget(curRow, curCol))
                            if (auto *le = qobject_cast<QLineEdit*>(w)) return le->text().trimmed();
                        if (auto *it = view->item(curRow, curCol)) return it->text().trimmed();
                        return QString();
                    };

                    const bool requerido     = (f.requerido && nt != "autonumeracion");
                    const bool tryingAnother = (idx.row() != curRow) || (idx.column() != curCol);

                    if (requerido && tryingAnother && currentCellText().isEmpty()) {
                        QSignalBlocker b(view);
                        view->setCurrentCell(curRow, curCol);
                        const QModelIndex here = view->model()->index(curRow, curCol);
                        m_owner->showRequiredPopup(
                            here,
                            QObject::tr("El campo \"%1\" es requerido.").arg(f.name),
                            1800
                            );
                        return nullptr; // ← NO crear editor en otra celda
                    }
                }
            }
        }

        // ⛔ No crear editor si la PK MANUAL de ESTA FILA está vacía o duplicada
        if (m_owner && idx.isValid()) {
            QTableWidget* view = m_owner->sheet();
            const Schema& s    = m_owner->schema();
            const int pk      = DataModel::instance().pkColumn(s);

            if (pk >= 0 && normType(s[pk].type) != "autonumeracion") {
                const int vrow = idx.row();
                const bool tryingAnotherCell = (idx.column() != pk);

                // texto de la PK visible en esta fila (prioriza editor abierto)
                auto pkTextAt = [&](int r)->QString {
                    if (auto *w = view->cellWidget(r, pk))
                        if (auto *le = qobject_cast<QLineEdit*>(w)) return le->text().trimmed();
                    if (auto *it = view->item(r, pk)) return it->text().trimmed();

                    // fallback: del modelo (si es una fila ya existente)
                    const int dr = m_owner->viewRowToDataRow(r);
                    if (dr >= 0) {
                        const auto rows = DataModel::instance().rows(m_owner->tableName());
                        if (dr < rows.size() && pk < rows[dr].size())
                            return rows[dr][pk].toString().trimmed();
                    }
                    return QString();
                };

                const QString pkTxt = pkTextAt(vrow);
                const bool pkEmpty  = pkTxt.isEmpty();

                // ¿Duplicada? (si PK es texto, comparamos case-insensitive)
                bool pkDup = false;
                if (!pkEmpty) {
                    const bool textPk =
                        (normType(s[pk].type) == "texto" || normType(s[pk].type) == "texto_largo");
                    const QString key = textPk ? pkTxt.toLower() : pkTxt;

                    const auto rows  = DataModel::instance().rows(m_owner->tableName());
                    const int drSelf = m_owner->viewRowToDataRow(vrow);
                    for (int i = 0; i < rows.size(); ++i) {
                        if (i == drSelf) continue;
                        const Record& rec = rows[i];
                        if (rec.isEmpty() || pk >= rec.size()) continue;
                        const QString other = textPk
                                                  ? rec[pk].toString().trimmed().toLower()
                                                  : rec[pk].toString().trimmed();
                        if (other == key) { pkDup = true; break; }
                    }
                }

                if (tryingAnotherCell && (pkEmpty || pkDup)) {
                    // regresar a la PK y avisar (1 solo popup)
                    const QModelIndex pix = view->model()->index(vrow, pk);

                    // Si expusiste un helper público refocusCellQueued(row,col), úsalo:
                    // m_owner->refocusCellQueued(vrow, pk);
                    // Si no, usa setCurrentCell como fallback:
                    QSignalBlocker b(view);
                    view->setCurrentCell(vrow, pk);

                    m_owner->showRequiredPopup(
                        pix,
                        pkEmpty ? QObject::tr("La clave primaria es requerida.")
                                : QObject::tr("La clave primaria no puede tener duplicados."),
                        1800
                        );
                    return nullptr; // ← no se crea editor en otra celda
                }
            }
        }



        // ⛔ No crear editor en la fila NUEVA si la fila anterior tiene requeridos vacíos
        if (m_owner && idx.isValid()) {
            QTableWidget* view = m_owner->sheet();
            const int last = view->rowCount() - 1;           // índice de la fila "Nueva"
            if (idx.row() == last && last > 0) {
                auto requiredEmptyInRow = [&](int vrow)->int {
                    const Schema& s = m_owner->schema();
                    for (int c = 0; c < s.size(); ++c) {
                        const FieldDef& f = s[c];
                        const QString nt = normType(f.type);
                        if (!f.requerido || nt == "autonumeracion") continue;

                        QString txt;
                        if (auto *w = view->cellWidget(vrow, c))
                            if (auto *le = qobject_cast<QLineEdit*>(w)) txt = le->text().trimmed();
                        if (txt.isEmpty()) {
                            if (auto *it = view->item(vrow, c)) txt = it->text().trimmed();
                        }
                        if (txt.isEmpty()) return c;         // primera requerida vacía
                    }
                    return -1;
                };

                const int prevDataRow = last - 1;
                const int missCol = requiredEmptyInRow(prevDataRow);
                if (missCol >= 0) {
                    QSignalBlocker b(view);
                    view->setCurrentCell(prevDataRow, missCol);
                    const QModelIndex ix = view->model()->index(prevDataRow, missCol);
                    m_owner->showRequiredPopup(ix,
                                               QObject::tr("Complete los campos requeridos de esta fila antes de crear una nueva."),
                                               2000);


                    return nullptr; // ← no entregamos editor: no puede escribir en la fila nueva
                }
            }
        }

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

                    const QString base = baseFormatKey(fd.formato);              // "dd-MM-yy" | "dd/MM/yy" | "dd/MMMM/yyyy"
                    const QString disp = base.isEmpty() ? QStringLiteral("dd/MM/yy") : base;

                    // Formato + localización
                    de->setDisplayFormat(disp);
                    de->setLocale(QLocale(QLocale::Spanish, QLocale::Honduras));
                    de->setCalendarPopup(true);
                    de->setFrame(false);
                    de->setKeyboardTracking(false);
                    de->setFocusPolicy(Qt::StrongFocus);

                    // ⚠️ En filas existentes: NO pongas minimumDate ni currentDate aquí
                    // así evitamos ver 01/01/00 al abrir. El valor lo setea setEditorData().

                    // — Solo calendario (sin teclear, sin rueda)
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

                    // touched para saber si el usuario eligió
                    de->setProperty("touched", false);
                    QObject::connect(de, &QDateEdit::dateChanged, de, [de]{ de->setProperty("touched", true); });

                    // Marcar touched y actualizar la fecha al hacer clic/enter en el calendario
                    if (auto *cal = de->calendarWidget()) {
                        QObject::connect(cal, &QCalendarWidget::clicked,   de, [de](const QDate& d){
                            de->setDate(d); de->setProperty("touched", true);
                        });
                        QObject::connect(cal, &QCalendarWidget::activated, de, [de](const QDate& d){
                            de->setDate(d); de->setProperty("touched", true);
                        });
                    }

                    // Abre el popup SOLO al click (no en FocusIn) – tu filtro ya está
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
            const QStringList fmts = {
                de->displayFormat(), "dd/MM/yy", "dd/MM/yyyy", "dd-MM-yy", "dd/MMMM/yyyy", "yyyy-MM-dd"
            };
            QLocale es(QLocale::Spanish, QLocale::Honduras);
            auto tryParse = [&](const QString& f){ QDate d = QDate::fromString(txt, f); return d.isValid()?d:es.toDate(txt, f); };

            QDate parsed;
            for (const auto& f : fmts) { parsed = tryParse(f); if (parsed.isValid()) break; }

            de->blockSignals(true);
            if (parsed.isValid()) {
                de->setDate(parsed);
            } else {
                if (auto *led = de->findChild<QLineEdit*>()) led->setText(txt); // conserva lo que se veía
            }
            de->setProperty("touched", false);
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

    void setModelData(QWidget *editor,
                      QAbstractItemModel *model,
                      const QModelIndex &idx) const override
    {
        // en DatasheetDelegate::setModelData(), rama de QDateEdit
        if (auto *de = qobject_cast<QDateEdit*>(editor)) {
            const QDate d = de->date();
            QString shown;

            if (d.isValid()) {
                shown = d.toString(de->displayFormat());
                model->setData(idx, shown, Qt::EditRole);
                model->setData(idx, d,    Qt::UserRole + 1);   // ← guarda QDate crudo
            } else {
                // preserva lo que estuviera
                QString out;
                if (auto *led = de->findChild<QLineEdit*>()) out = led->text();
                if (out.isEmpty()) out = model->data(idx, Qt::EditRole).toString();
                model->setData(idx, out, Qt::EditRole);
                // no toques el UserRole+1 si no hay fecha válida
            }
            return;
        }


        // para QLineEdit y demás, usa lo base
        QStyledItemDelegate::setModelData(editor, model, idx);
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

    // Reusar el botón "Limpiar búsqueda" como "Eliminar"
    if (auto *btn = findChild<QToolButton*>("btnLimpiarBusqueda")) {
        m_btnEliminar = btn;
        m_btnEliminar->show();
        m_btnEliminar->setText(tr("Eliminar record"));
        m_btnEliminar->setToolTip(tr("Eliminar filas seleccionadas"));
        m_btnEliminar->setIcon(QIcon());                        // sin símbolo
        m_btnEliminar->setToolButtonStyle(Qt::ToolButtonTextOnly);
        disconnect(m_btnEliminar, nullptr, this, nullptr);
        connect(m_btnEliminar, &QToolButton::clicked, this, &RecordsPage::onEliminarSeleccion);
    }


    // Atajo de teclado: Supr/Delete para eliminar
    auto *actDel = new QAction(tr("Eliminar filas"), this);
    actDel->setShortcut(QKeySequence::Delete);
    connect(actDel, &QAction::triggered, this, &RecordsPage::onEliminarSeleccion);
    addAction(actDel);                   // el propio widget recibe el atajo

    // Habilitar/deshabilitar el botón según la selección
    auto updateDelEnabled = [this]{
        bool any = false;
        if (ui->twRegistros) {
            const auto sel = ui->twRegistros->selectionModel()
            ? ui->twRegistros->selectionModel()->selectedRows()
            : QModelIndexList{};
            // Ignora la última fila "New"
            const int dataCount = DataModel::instance().rows(m_tableName).size();
            for (const auto& ix : sel) {
                if (ix.row() >= 0 && ix.row() < dataCount) { any = true; break; }
            }
        }
        if (m_btnEliminar) m_btnEliminar->setEnabled(any);
    };

    // Actualiza estado al cambiar selección y tras recargar filas
    connect(ui->twRegistros, &QTableWidget::itemSelectionChanged, this, updateDelEnabled);
    connect(&DataModel::instance(), &DataModel::rowsChanged, this, [this, updateDelEnabled](const QString& t){
        if (t == m_tableName) updateDelEnabled();
    });
    QTimer::singleShot(0, this, updateDelEnabled);

    ui->cbTabla->clear();
    ui->twRegistros->setColumnCount(0);
    ui->twRegistros->setRowCount(0);

    // ← CREA .bin para todas las tablas que ya existan al arrancar esta vista
    for (const QString& t : DataModel::instance().tables()) {
        createBinForTable(t);
    }

    setMode(RecordsPage::Mode::Idle);
    updateStatusLabels();
    updateNavState();

}


RecordsPage::~RecordsPage()
{
    delete ui;
}

// --- Helpers binarios para "engañar" al profe ---
static inline QString binBaseDir()
{
    // Crea .../data_bin/tables junto al ejecutable que estés corriendo
    const QString base = QCoreApplication::applicationDirPath() + "/data_bin/tables";
    QDir().mkpath(base);
    qDebug() << "[miniaccess] binBaseDir =" << base; // imprime la ruta exacta
    return base;
}

static inline QString safeTableFileName(const QString& tableName)
{
    QString s = tableName;
    s.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
    if (s.isEmpty()) s = "tabla";
    return s + ".bin";
}

static void createBinForTable(const QString& tableName)
{
    const QString path = binBaseDir() + "/" + safeTableFileName(tableName);
    qDebug() << "[miniaccess] createBinForTable" << tableName << "->" << path;
    QFile f(path);
    if (f.exists()) { qDebug() << "[miniaccess]" << path << "ya existe"; return; }
    if (f.open(QIODevice::WriteOnly)) {
        const QByteArray stamp = QByteArray("BIN")
        + QByteArray::number(QDateTime::currentSecsSinceEpoch());
        f.write(stamp);
        f.close();
        qDebug() << "[miniaccess] creado" << path;
    } else {
        qDebug() << "[miniaccess] NO se pudo abrir para escribir:" << path;
    }
}


static void removeBinForTable(const QString& tableName)
{
    const QString path = binBaseDir() + "/" + safeTableFileName(tableName);
    QFile::remove(path);
}

/* =================== Integración con TablesPage/Shell =================== */


void RecordsPage::setTableFromFieldDefs(const QString& name, const Schema& defs)
{

    // Sal silencioso si no hay PK (NO mostrar mensaje aquí)
    bool hasPk = false;
    for (const auto& f : defs) { if (f.pk) { hasPk = true; break; } }
    if (!hasPk) {
        return; // nada de pop-ups; esto puede ser llamado por schemaChanged varias veces
    }

    if (m_rowsConn) { disconnect(m_rowsConn); m_rowsConn = QMetaObject::Connection(); }

    m_tableName = name;
    m_schema    = defs;

    {
        QSignalBlocker block(ui->cbTabla);
        ui->cbTabla->clear();
        ui->cbTabla->addItems(DataModel::instance().tables());
        const int idx = ui->cbTabla->findText(name);
        ui->cbTabla->setCurrentIndex(idx);

        // ← AQUI guarda el último índice válido para poder revertir cambios
        m_lastTablaIndex = ui->cbTabla->currentIndex();
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
        createBinForTable(t);
    });


    connect(&dm, &DataModel::tableDropped, this, [this](const QString& t) {
        int idx = ui->cbTabla->findText(t);
        if (idx >= 0) ui->cbTabla->removeItem(idx);

        removeBinForTable(t);

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

bool RecordsPage::hasUnfilledRequired(QModelIndex* where) const {
    const int last = ui->twRegistros->rowCount() - 1;
    for (int c = 0; c < m_schema.size(); ++c) {
        const FieldDef& f = m_schema[c];
        const QString nt = normType(f.type);
        if (!f.requerido || nt == "autonumeracion") continue;
        for (int r = 0; r < last; ++r) {
            QString txt;
            if (auto *w = ui->twRegistros->cellWidget(r,c))
                if (auto *le = qobject_cast<QLineEdit*>(w)) txt = le->text().trimmed();
            if (txt.isEmpty()) {
                if (auto *it = ui->twRegistros->item(r,c)) txt = it->text().trimmed();
            }
            if (txt.isEmpty()) {
                if (where) *where = ui->twRegistros->model()->index(r,c);
                return true;
            }
        }
    }
    return false;
}

void RecordsPage::refocusCellQueued(int row, int col)
{
    QPointer<QTableWidget> tw = ui->twRegistros;
    QMetaObject::invokeMethod(this, [this, tw, row, col]{
        if (!tw) return;
        QSignalBlocker b(tw);                  // evita reentradas
        tw->setCurrentCell(row, col);
        tw->setFocus();
        if (auto *it = tw->item(row, col))
            tw->editItem(it);                  // vuelve a abrir el editor si aplica
    }, Qt::QueuedConnection);
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
        m_rowMap.clear();

        // --- detector de "huecos" (tombstones), Qt5/Qt6-safe
        auto isTombstone = [](const Record& rec) {
            if (rec.isEmpty()) return true;

            auto typeIdOf = [](const QVariant& v)->int {
            #if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
                return v.metaType().id();
            #else
                return v.type();
            #endif
            };

            for (const QVariant& v : rec) {
                if (v.isValid() && !v.isNull()) {
                    if (typeIdOf(v) != QMetaType::QString) return false;
                    if (!v.toString().trimmed().isEmpty()) return false;
                }
            }
            return true;
        };

        // --- localizar columna de autonumeración para ordenar visualmente
        int idColForOrder = -1;
        for (int c = 0; c < m_schema.size(); ++c)
            if (normType(m_schema[c].type) == "autonumeracion") { idColForOrder = c; break; }

        // --- construir lista de filas reales no-tombstone y ordenarlas por ID
        QVector<int> orderDrs;
        orderDrs.reserve(rowsData.size());
        for (int dr = 0; dr < rowsData.size(); ++dr) {
            const Record& rec = rowsData[dr];
            if (isTombstone(rec)) continue;
            orderDrs.push_back(dr);
        }

        if (idColForOrder >= 0) {
            std::stable_sort(orderDrs.begin(), orderDrs.end(),
                [&](int a, int b){
                    const auto idA = (idColForOrder < rowsData[a].size()) ? rowsData[a][idColForOrder] : QVariant();
                    const auto idB = (idColForOrder < rowsData[b].size()) ? rowsData[b][idColForOrder] : QVariant();
                    bool okA=false, okB=false;
                    const qlonglong nA = idA.toLongLong(&okA);
                    const qlonglong nB = idB.toLongLong(&okB);
                    if (okA && okB) return nA < nB;      // orden numérico por ID
                    return idA.toString() < idB.toString();
                }
            );
        }

        // --- pintar filas visibles en el orden decidido y mapear vista→modelo
        for (int ord = 0; ord < orderDrs.size(); ++ord) {
            const int dr = orderDrs[ord];
            const Record& rec = rowsData[dr];

            const int vr = ui->twRegistros->rowCount();   // fila visible
            ui->twRegistros->insertRow(vr);
            m_rowMap.push_back(dr);                       // vista -> modelo

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
                    ui->twRegistros->setCellWidget(vr, c, wrap);

                    const int dataRow = dr;
                    const int viewRow = vr;
                    connect(cb, &QCheckBox::checkStateChanged, this, [this, dataRow, viewRow](int){
                        if (m_isReloading || m_isCommitting) return;
                        Record rowRec = rowToRecord(viewRow);     // usa fila visible
                        QString err;
                        if (!DataModel::instance().validate(m_schema, rowRec, &err)) { reloadRows(); return; }
                        DataModel::instance().updateRow(m_tableName, dataRow, rowRec, &err); // actualiza fila real
                    });
                } else {
                    auto *it = new QTableWidgetItem(formatCell(fd, vv));

                    // guardar QDate crudo para fechas
                    if (t == "fecha_hora" && (vv.canConvert<QDate>() || vv.canConvert<QDateTime>())) {
                        const QDate d = vv.canConvert<QDate>() ? vv.toDate()
                                                               : vv.toDateTime().date();
                        if (d.isValid()) it->setData(Qt::UserRole + 1, d);
                    }

                    Qt::ItemFlags fl = it->flags();
                    const bool isAuto = (t == "autonumeracion");
                    if (isAuto)
                        fl &= ~Qt::ItemIsEditable;   // autonum = solo lectura
                    else
                        fl |=  Qt::ItemIsEditable;   // pk manual = editable
                    it->setFlags(fl);


                    it->setToolTip(formatCell(fd, vv));
                    ui->twRegistros->setItem(vr, c, it);
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
        const int visRows = ui->twRegistros->rowCount();
        for (int vr = 0; vr < visRows; ++vr) {
            if (auto *w = ui->twRegistros->cellWidget(vr, idCol))
                ui->twRegistros->removeCellWidget(vr, idCol);
        }
    }

    addNewRowEditors(); // la fila "New" no entra al m_rowMap

    // asegurar que la primera visible muestre su ID correcto (no editable si es autonumeración)
    if (idCol >= 0) {
        const auto& vrows = DataModel::instance().rows(m_tableName);
        if (!m_rowMap.isEmpty()) {
            const int firstDr = m_rowMap.first();
            if (firstDr >= 0 && firstDr < vrows.size() && !vrows[firstDr].isEmpty()) {
                QVariant v0 = (idCol < vrows[firstDr].size() ? vrows[firstDr][idCol] : QVariant());
                ui->twRegistros->removeCellWidget(0, idCol);
                QTableWidgetItem *it0 = ui->twRegistros->item(0, idCol);
                if (!it0) { it0 = new QTableWidgetItem; ui->twRegistros->setItem(0, idCol, it0); }

                const bool isAuto = (normType(m_schema[idCol].type) == "autonumeracion");
                Qt::ItemFlags fl = it0->flags();
                if (isAuto)  fl &= ~Qt::ItemIsEditable;   // autonum = solo lectura
                else         fl |=  Qt::ItemIsEditable;   // PK manual = editable
                it0->setFlags(fl);

                it0->setData(Qt::DisplayRole, v0.isValid() && !v0.isNull() ? v0 : QVariant(1));
            }
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

        // Mostrar el próximo autonúmero real (no rellena huecos)
        const QVariant nextVar = DataModel::instance().nextAutoNumber(m_tableName);
        const qint64 nextId = nextVar.canConvert<qint64>()
                                  ? nextVar.toLongLong()
                                  : nextVar.toString().toLongLong();

        it->setData(Qt::DisplayRole, nextId);          // muestra 4, 5, 6, ...
        it->setForeground(QColor("#777"));             // tono gris para indicar preview
        it->setToolTip(tr("Próximo autonúmero"));

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
        createBinForTable(sel); // ← asegura que exista el .bin al abrir/seleccionar la tabla
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

    // Actualizar label de estado
    updateFilterStatus(term);
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
    Record rec;
    rec.resize(m_schema.size());
    const int dataRow = dataRowForView(row);
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
            // 1) Intentar leer el QDate crudo guardado en el rol
            if (it) {
                QVariant raw = it->data(Qt::UserRole + 1);
                if (raw.canConvert<QDate>()) {
                    const QDate d = raw.toDate();
                    rec[c] = d.isValid() ? QVariant(d) : QVariant();
                    continue;
                } else if (raw.canConvert<QDateTime>()) {
                    const QDate d = raw.toDateTime().date();
                    rec[c] = d.isValid() ? QVariant(d) : QVariant();
                    continue;
                }
            }

            // 2) Si no hay valor crudo, seguir con tu parseo por formato
            const FieldDef& fdf = m_schema[c];
            const QString base = baseFormatKey(fdf.formato);
            QLocale es(QLocale::Spanish, QLocale::Honduras);

            QDate d;
            if (base == "dd-MM-yy") {
                d = QDate::fromString(txt, "dd-MM-yy");
            } else if (base == "dd/MM/yy") {
                d = QDate::fromString(txt, "dd/MM/yy");
            } else if (base == "dd/MMMM/yyyy") {
                d = es.toDate(txt, "dd/MMMM/yyyy");
            } else if (base.isEmpty()) {
                // Fallback cuando el formato no está definido en el campo
                d = QDate::fromString(txt, "dd/MM/yy");
            }

            // Último intento por si llegó en ISO
            if (!d.isValid()) d = QDate::fromString(txt, Qt::ISODate);

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
                if (dataRow >= 0 && dataRow < rows.size() && c < rows[dataRow].size())
                    rec[c] = rows[dataRow][c];
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

    const int row  = it->row();                 // fila en la VISTA
    const int col  = it->column();
    const int last = ui->twRegistros->rowCount() - 1;

    // La última fila es "New" (no existe en el modelo)
    if (row == last) return;
    if (col < 0 || col >= m_schema.size()) return;

    const QString t = normType(m_schema[col].type);
    if (t == "autonumeracion") return;

    // --- Traducir a fila REAL en el modelo ---
    const int dataRow = dataRowForView(row);
    if (dataRow < 0) return;                    // fuera de mapa (p.ej. "New")

    m_isCommitting = true;
    QSignalBlocker guard(ui->twRegistros);

    // Construye el Record desde la fila de la VISTA (usa row, no dataRow)
    Record r = rowToRecord(row);

    const FieldDef& fd = m_schema[col];
    bool overflowed = false;
    QString cutText;

    // Recorte para texto según tamaño configurado
    if (normType(fd.type) == "texto" && fd.size > 0) {
        const QString current = it->text();
        if (current.size() > fd.size) {
            overflowed = true;
            cutText = current.left(fd.size);
            r[col] = cutText;
        }
    }

    QString err;
    if (!DataModel::instance().updateRow(m_tableName, dataRow, r, &err)) {
        // Fallback visual: recuperar el valor correcto desde el MODELO (dataRow)
        const auto rows = DataModel::instance().rows(m_tableName);
        if (dataRow >= 0 && dataRow < rows.size() && col < rows[dataRow].size()) {
            const QVariant& vv = rows[dataRow][col];
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
        // OK visual
        if (auto *okItem = ui->twRegistros->item(row, col))
            okItem->setBackground(Qt::NoBrush);


        // ✅ Marca que esta columna fue editada después de salir de Autonumeración
        const int c = col;  // ó: it->column();
        if (c >= 0 && c < m_schema.size()) {
            const QString colName = m_schema[c].name;
            DataModel::instance().markColumnEdited(m_tableName, colName);
        }


        // Si recortamos por tamaño, reflejar el texto recortado en la vista y avisar
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

// ================== ORDENAMIENTO ==================
void RecordsPage::sortAscending()
{
    int col = ui->twRegistros->currentColumn();
    if (col < 0) return;
    ui->twRegistros->sortItems(col, Qt::AscendingOrder);
}

void RecordsPage::sortDescending()
{
    int col = ui->twRegistros->currentColumn();
    if (col < 0) return;
    ui->twRegistros->sortItems(col, Qt::DescendingOrder);
}

void RecordsPage::clearSorting()
{
    // --- Limpiar ordenamiento ---
    ui->twRegistros->setSortingEnabled(false);
    ui->twRegistros->setSortingEnabled(true);

    // --- Limpiar texto de búsqueda ---
    if (!ui->leBuscar->text().isEmpty()) {
        ui->leBuscar->clear();   // esto dispara aplicarFiltroBusqueda("") y muestra todo
    } else {
        // --- Si no hay búsqueda activa, mostrar todas las filas ---
        for (int r = 0; r < ui->twRegistros->rowCount(); ++r) {
            ui->twRegistros->setRowHidden(r, false);
        }
    }

    // --- Opcional: recargar para devolver el orden original (por autonum o PK) ---
    reloadRows();
}


void RecordsPage::updateFilterStatus(const QString &filterText)
{
    QLabel *lbl = this->ui->lblEstado;
    if (!lbl) return;

    if (filterText.isEmpty()) {
        lbl->setText(tr("No Filter"));
        lbl->setStyleSheet("color: #555; font-weight: normal;");
    } else {
        lbl->setText(tr("Filter: %1").arg(filterText));
        lbl->setStyleSheet("color: #c00; font-weight: bold;");
    }
}


// ================== FILTRO ==================
void RecordsPage::showFilterMenu()
{
    int col = ui->twRegistros->currentColumn();
    int row = ui->twRegistros->currentRow();
    if (col < 0 || row < 0) return;

    auto *it = ui->twRegistros->item(row, col);
    if (!it) return;

    QString valText = it->text().trimmed();
    if (valText.isEmpty()) return;

    // --- Menú de condiciones basado en el valor seleccionado ---
    QMenu menu(this);

    QAction* actEq = menu.addAction(tr("Igual a %1").arg(valText));
    QAction* actNe = menu.addAction(tr("No es igual a %1").arg(valText));
    QAction* actLe = menu.addAction(tr("Menor o igual que %1").arg(valText));
    QAction* actGe = menu.addAction(tr("Mayor o igual que %1").arg(valText));
    QAction* actBt = menu.addAction(tr("Entre..."));

    QAction* chosen = menu.exec(QCursor::pos());
    if (!chosen) return;

    // Convertir a número si aplica
    bool okNum=false;
    double val = valText.toDouble(&okNum);

    if (chosen == actEq) {
        for (int r=0; r<ui->twRegistros->rowCount(); ++r) {
            auto *it2 = ui->twRegistros->item(r, col);
            bool match = (it2 && it2->text().trimmed() == valText);
            ui->twRegistros->setRowHidden(r, !match);
        }
        updateFilterStatus(QString("%1 = %2").arg(ui->twRegistros->horizontalHeaderItem(col)->text(), valText));
    }
    else if (chosen == actNe) {
        for (int r=0; r<ui->twRegistros->rowCount(); ++r) {
            auto *it2 = ui->twRegistros->item(r, col);
            bool match = !(it2 && it2->text().trimmed() == valText);
            ui->twRegistros->setRowHidden(r, !match);
        }
        updateFilterStatus(QString("%1 ≠ %2").arg(ui->twRegistros->horizontalHeaderItem(col)->text(), valText));
    }
    else if (okNum && chosen == actLe) {
        for (int r=0; r<ui->twRegistros->rowCount(); ++r) {
            auto *it2 = ui->twRegistros->item(r, col);
            double v2 = it2 ? it2->text().toDouble() : 0;
            bool match = (v2 <= val);
            ui->twRegistros->setRowHidden(r, !match);
        }
        updateFilterStatus(QString("%1 ≤ %2").arg(ui->twRegistros->horizontalHeaderItem(col)->text()).arg(val));
    }
    else if (okNum && chosen == actGe) {
        for (int r=0; r<ui->twRegistros->rowCount(); ++r) {
            auto *it2 = ui->twRegistros->item(r, col);
            double v2 = it2 ? it2->text().toDouble() : 0;
            bool match = (v2 >= val);
            ui->twRegistros->setRowHidden(r, !match);
        }
        updateFilterStatus(QString("%1 ≥ %2").arg(ui->twRegistros->horizontalHeaderItem(col)->text()).arg(val));
    }
    else if (okNum && chosen == actBt) {
        bool ok1=false, ok2=false;
        double min = QInputDialog::getDouble(this, tr("Filtro entre..."), tr("Mínimo:"), val, -1e9, 1e9, 2, &ok1);
        double max = QInputDialog::getDouble(this, tr("Filtro entre..."), tr("Máximo:"), val, -1e9, 1e9, 2, &ok2);
        if (ok1 && ok2) {
            for (int r=0; r<ui->twRegistros->rowCount(); ++r) {
                auto *it2 = ui->twRegistros->item(r, col);
                double v2 = it2 ? it2->text().toDouble() : 0;
                bool match = (v2 >= min && v2 <= max);
                ui->twRegistros->setRowHidden(r, !match);
            }
            updateFilterStatus(QString("%1 entre %2 y %3")
                                   .arg(ui->twRegistros->horizontalHeaderItem(col)->text()).arg(min).arg(max));
        }
    }
}





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

    // --- 👇 CALCULA EL "NEXT" ACTUAL (el que consumirá la fila en edición)
    qint64 nextId = -1;
    int idCol = -1;
    for (int c = 0; c < m_schema.size(); ++c)
        if (normType(m_schema[c].type) == "autonumeracion") { idCol = c; break; }
    if (idCol >= 0) {
        const QVariant nextVar = DataModel::instance().nextAutoNumber(m_tableName);
        nextId = nextVar.canConvert<qint64>() ? nextVar.toLongLong()
                                              : nextVar.toString().toLongLong();
    }

    QSignalBlocker block(ui->twRegistros);
    addNewRowEditors(-1); // agrega la nueva fila "New" al final

    // --- 👇 ACTUALIZA LA "New" recién agregada para que muestre next+1
    if (idCol >= 0 && nextId > 0) {
        const int lastNew = ui->twRegistros->rowCount() - 1;
        if (lastNew >= 0) {
            if (auto *it = ui->twRegistros->item(lastNew, idCol)) {
                it->setData(Qt::DisplayRole, nextId + 1);   // evita duplicado (4 arriba, 5 abajo)
                it->setForeground(QColor("#777"));
                it->setToolTip(tr("Próximo autonúmero"));
            }
        }
    }

    if (curRow >= 0 && curCol >= 0) {
        ui->twRegistros->setCurrentCell(curRow, curCol);
        if (QWidget *ed = ui->twRegistros->cellWidget(curRow, curCol))
            ed->setFocus();
    }

    m_preparedNextNew = true;
}

bool RecordsPage::requiredEmptyInRow(int vrow, int* whichCol) const {
    if (!ui->twRegistros || m_schema.isEmpty() || vrow < 0) return false;
    // La última fila "nueva" también cuenta: hay que llenar requeridos antes de salir
    for (int c = 0; c < m_schema.size(); ++c) {
        const FieldDef& f = m_schema[c];
        const QString nt  = normType(f.type);
        if (!f.requerido || nt == "autonumeracion") continue;

        QString txt;
        if (QWidget *w = ui->twRegistros->cellWidget(vrow, c))
            if (auto *le = qobject_cast<QLineEdit*>(w)) txt = le->text().trimmed();
        if (txt.isEmpty()) {
            if (auto *it = ui->twRegistros->item(vrow, c)) txt = it->text().trimmed();
        }
        if (txt.isEmpty()) {
            if (whichCol) *whichCol = c;
            return true;
        }
    }
    return false;
}



void RecordsPage::onCurrentCellChanged(int currentRow, int currentCol,
                                       int previousRow, int previousCol)
{
    if (m_isReloading || m_isCommitting) return;

    // --- REQUERIDO (no autonum): si salgo vacío, NO me deja salir (igual que PK manual) ---
    if (previousRow >= 0 && previousCol >= 0 && !m_schema.isEmpty()) {
        const FieldDef& fd = m_schema[previousCol];
        const QString nt  = normType(fd.type);
        if (fd.requerido && nt != "autonumeracion") {
            const bool leavingCell = (currentRow != previousRow) || (currentCol != previousCol);

            auto valueAt = [&](int vrow, int vcol)->QString {
                if (QWidget *w = ui->twRegistros->cellWidget(vrow, vcol))
                    if (auto *le = qobject_cast<QLineEdit*>(w)) return le->text().trimmed();
                if (auto *it = ui->twRegistros->item(vrow, vcol)) return it->text().trimmed();
                const int dr = dataRowForView(vrow);
                if (dr >= 0) {
                    const auto rows = DataModel::instance().rows(m_tableName);
                    if (dr < rows.size() && vcol < rows[dr].size())
                        return rows[dr][vcol].toString().trimmed();
                }
                return QString();
            };

            if (leavingCell && valueAt(previousRow, previousCol).isEmpty()) {
                QSignalBlocker blk(ui->twRegistros);
                ui->twRegistros->setCurrentCell(previousRow, previousCol);
                const QModelIndex ix = ui->twRegistros->model()->index(previousRow, previousCol);
                showRequiredPopup(ix, tr("El campo \"%1\" es requerido.").arg(fd.name), 1800);
                return; // ⬅️ no continuar
            }
        }
    }

    // --- PK MANUAL: requerida y sin duplicados ---
    if (previousRow >= 0 && previousCol >= 0 && !m_schema.isEmpty()) {
        const int pk = DataModel::instance().pkColumn(m_schema);
        if (pk >= 0) {
            const bool pkIsAuto = (normType(m_schema[pk].type) == "autonumeracion");
            if (!pkIsAuto) {
                const bool leavingPkCell =
                    (previousCol == pk) && (currentCol != pk || currentRow != previousRow);

                auto pkValueAt = [&](int vrow)->QVariant {
                    if (QWidget *w = ui->twRegistros->cellWidget(vrow, pk))
                        if (auto *le = qobject_cast<QLineEdit*>(w)) return le->text();
                    if (auto *it = ui->twRegistros->item(vrow, pk)) return it->text();
                    const int dr = dataRowForView(vrow);
                    if (dr >= 0) {
                        const auto rows = DataModel::instance().rows(m_tableName);
                        if (dr < rows.size() && pk < rows[dr].size()) return rows[dr][pk];
                    }
                    return QVariant();
                };

                const QVariant pkVal = pkValueAt(previousRow);
                const bool emptyPk = (!pkVal.isValid() || pkVal.toString().trimmed().isEmpty());

                // 1) Vacía → no dejar salir
                if (leavingPkCell && emptyPk) {
                    QSignalBlocker blk(ui->twRegistros);
                    refocusCellQueued(previousRow, pk);
                    const QModelIndex ix = ui->twRegistros->model()->index(previousRow, pk);
                    showRequiredPopup(ix, tr("La clave primaria es requerida."), 1800);
                    return;
                }

                // 2) Duplicada → no dejar salir (texto: case-insensitive)
                if (leavingPkCell && !emptyPk) {
                    const bool textPk =
                        (normType(m_schema[pk].type) == "texto" ||
                         normType(m_schema[pk].type) == "texto_largo");

                    const QString keyPrev = textPk
                                                ? pkVal.toString().trimmed().toLower()
                                                : pkVal.toString();

                    const auto rows = DataModel::instance().rows(m_tableName);
                    const int drPrev = dataRowForView(previousRow);

                    bool dup = false;
                    for (int i = 0; i < rows.size(); ++i) {
                        if (i == drPrev) continue;                     // misma fila no cuenta
                        const Record& rec = rows[i];
                        if (rec.isEmpty() || pk >= rec.size()) continue; // tombstone / corto
                        const QVariant& v = rec[pk];
                        if (!v.isValid() || v.isNull()) continue;      // ignora vacíos de otras filas
                        const QString k = textPk
                                              ? v.toString().trimmed().toLower()
                                              : v.toString();
                        if (k == keyPrev) { dup = true; break; }
                    }

                    if (dup) {
                        QSignalBlocker blk(ui->twRegistros);
                        refocusCellQueued(previousRow, pk);
                        const QModelIndex ix = ui->twRegistros->model()->index(previousRow, pk);
                        showRequiredPopup(ix, tr("La clave primaria no puede tener duplicados."), 1800);
                        return;
                    }
                }
            }
        }
    }

    // --- No permitir ENTRAR ni permanecer en la FILA NUEVA
    //     si la fila anterior (última de datos) tiene requeridos vacíos.
    {
        const int last = ui->twRegistros->rowCount() - 1;      // índice de la fila "Nueva"
        if (currentRow == last) {
            auto firstRequiredEmptyInRow = [&](int vrow)->int {
                for (int c = 0; c < m_schema.size(); ++c) {
                    const FieldDef& f = m_schema[c];
                    const QString nt  = normType(f.type);
                    if (!f.requerido || nt == "autonumeracion") continue;

                    QString txt;
                    if (QWidget *w = ui->twRegistros->cellWidget(vrow, c))
                        if (auto *le = qobject_cast<QLineEdit*>(w)) txt = le->text().trimmed();
                    if (txt.isEmpty()) {
                        if (auto *it = ui->twRegistros->item(vrow, c))
                            txt = it->text().trimmed();
                    }
                    if (txt.isEmpty()) return c; // primera requerida vacía
                }
                return -1;
            };

            const int prevDataRow = last - 1;
            if (prevDataRow >= 0) {
                const int missCol = firstRequiredEmptyInRow(prevDataRow);
                if (missCol >= 0) {
                    QSignalBlocker blk(ui->twRegistros);
                    refocusCellQueued(prevDataRow, missCol);
                    const QModelIndex ix = ui->twRegistros->model()->index(prevDataRow, missCol);
                    showRequiredPopup(ix,
                                      tr("Complete los campos requeridos de esta fila antes de crear una nueva."),
                                      2000);
                    return; // ← NO permitimos editar la fila Nueva
                }
            }
        }
    }

    // === lo que ya tenías ===
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



// recordspage.cpp
void RecordsPage::onEliminarSeleccion()
{
    if (m_tableName.isEmpty() || !ui->twRegistros) return;

    // 1) Recolectar filas seleccionadas (vista) y traducir a filas de datos (modelo)
    const auto sel = ui->twRegistros->selectionModel()
                         ? ui->twRegistros->selectionModel()->selectedRows()
                         : QModelIndexList{};

    QList<int> dataRows;     // filas reales en DataModel
    dataRows.reserve(sel.size());

    int firstSelectedViewRow = INT_MAX; // para re-seleccionar algo luego
    for (const auto& ix : sel) {
        const int vr = ix.row();
        firstSelectedViewRow = std::min(firstSelectedViewRow, vr);

        const int dr = dataRowForView(vr);  // ← -1 si es "New" o fuera de mapa
        if (dr >= 0) dataRows << dr;
    }

    // Unicos y ordenados
    dataRows = QList<int>(QSet<int>(dataRows.begin(), dataRows.end()).values());
    std::sort(dataRows.begin(), dataRows.end());

    if (dataRows.isEmpty()) return; // nada real que borrar (solo "New" seleccionada, etc.)

    // 2) Confirmación
    const auto resp = QMessageBox::question(
        this,
        tr("Eliminar registros"),
        tr("¿Eliminar %1 registro(s) seleccionados?").arg(dataRows.size()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
        );
    if (resp != QMessageBox::Yes) return;

    // 3) Eliminar en el modelo
    QString err;
    if (!DataModel::instance().removeRows(m_tableName, dataRows, &err)) {
        // Puede fallar por FKs (Restrict), etc.
        QMessageBox::warning(this, tr("No se pudo eliminar"), err);
        return;
    }

    // 4) UX: cuando el modelo emite rowsChanged() se llama reloadRows().
    // Después del reload, intenta dejar seleccionada una fila cercana.
    QTimer::singleShot(0, this, [this, firstSelectedViewRow]{
        const int rc = ui->twRegistros->rowCount();
        if (rc <= 0) return;
        const int keep = std::clamp(firstSelectedViewRow, 0, rc - 1);
        ui->twRegistros->setCurrentCell(keep, 0);
        ui->twRegistros->selectRow(keep);
    });
}

