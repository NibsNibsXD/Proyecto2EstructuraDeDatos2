#include "shellwindow.h"
#include "tablespage.h"
#include "recordspage.h"
#include "datamodel.h"
#include "querypage.h"   // <<< NUEVO
#include "accessquerydesigner.h" // <<< NUEVO: diseñador visual en clase aparte

// ====== Reportes (NUEVO) ======
#include "reportdef.h"
#include "reportengine.h"
#include "reportrenderer.h"
#include "reportwizard.h"
#include "reportspage.h"

#include <QGraphicsSimpleTextItem>
#include <QApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QButtonGroup>
#include <QLabel>
#include <QStackedWidget>
#include <QIcon>
#include <QWidget>
#include <QFrame>
#include <QStyle>
#include <QLineEdit>
#include <QListWidget>
#include <QAction>
#include <QMetaObject>
#include <QMessageBox>
#include <QInputDialog>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QCloseEvent>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QFontMetrics>
#include <QtMath>
#include <QGraphicsItem>
#include <QPointer>
#include <QMap>
#include <cmath>
#include <QVector2D>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>
#include <QMenu>
#include <QGraphicsSceneContextMenuEvent>
#include <functional>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QToolTip>
#include <QDockWidget> // <<< NUEVO
#include <QIntValidator>
#include <QDoubleValidator>
#include <QDateTimeEdit>
#include <limits>



// Tamaños base
static constexpr int kWinW   = 1400;
static constexpr int kWinH   = 800;
static constexpr int kTopH   = 45;   // barra iconos
static constexpr int kTabsH  = 45;   // barra tabs
static constexpr int kRibbonH= 100;  // "iconos menus" (contenedor)

// Área visible por debajo de las barras
static constexpr int kContentH = kWinH - (kTopH + kTabsH + kRibbonH); // 610
static constexpr int kLeftW  = 260;
static constexpr int kRightW = kWinW - kLeftW;                        // 1140

// Alturas en la derecha
static constexpr int kBottomReserveH = 36;                            // reserva inferior derecha
static constexpr int kStackH = kContentH - kBottomReserveH;          // 574

static QWidget* vSep(int h = 64) {
    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Plain);
    sep->setStyleSheet("color:#c8c8c8;");
    sep->setFixedHeight(h);
    return sep;
}


static bool showAddRelationDialog(QWidget* parent, const QString& tableHint = QString()) {
    auto &dm = DataModel::instance();

    QDialog dlg(parent);
    dlg.setWindowTitle("Modificar relaciones");
    dlg.setMinimumSize(560, 480);

    // ===== Layout principal =====
    auto *root = new QVBoxLayout(&dlg);

    // --- fila superior: tablas hijo / padre ---
    auto *top = new QWidget;
    auto *topHL = new QHBoxLayout(top);
    topHL->setContentsMargins(0,0,0,0);

    auto *leftBox  = new QVBoxLayout;
    auto *leftW  = new QWidget;  leftW->setLayout(leftBox);
    auto *rightBox = new QVBoxLayout;
    auto *rightW = new QWidget; rightW->setLayout(rightBox);

    auto *cbChildTable  = new QComboBox;
    auto *cbParentTable = new QComboBox;
    const QStringList tables = dm.tables();
    cbChildTable->addItems(tables);
    cbParentTable->addItems(tables);
    if (!tableHint.isEmpty()) {
        int ix = cbChildTable->findText(tableHint);
        if (ix>=0) cbChildTable->setCurrentIndex(ix);
    }

    leftBox->addWidget(new QLabel("Tabla o consulta"));
    leftBox->addWidget(cbChildTable);
    rightBox->addWidget(new QLabel("Tabla o consulta"));
    rightBox->addWidget(cbParentTable);
    topHL->addWidget(leftW, 1);
    topHL->addSpacing(12);
    topHL->addWidget(rightW, 1);

    // --- centro: grid de pares de campos ---
    auto *mid = new QWidget;
    auto *midVL = new QVBoxLayout(mid);
    midVL->setContentsMargins(0,0,0,0);

    auto *grid = new QTableWidget(0, 2);
    grid->setAlternatingRowColors(true);
    grid->verticalHeader()->setVisible(false);
    grid->horizontalHeader()->setStretchLastSection(true);
    grid->setSelectionMode(QAbstractItemView::SingleSelection);
    grid->setSelectionBehavior(QAbstractItemView::SelectRows);
    grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid->setShowGrid(true);
    grid->setHorizontalHeaderItem(0, new QTableWidgetItem(" "));
    grid->setHorizontalHeaderItem(1, new QTableWidgetItem(" "));

    auto *rowBtns = new QHBoxLayout;
    auto *btnAddRow    = new QPushButton("Agregar campo");
    auto *btnRemoveRow = new QPushButton("Quitar fila");
    auto *btnJoinType  = new QPushButton("Tipo de combinación...");
    btnJoinType->setEnabled(false);
    auto *btnJunction  = new QPushButton("Crear tabla de unión…");
    btnJunction->setEnabled(false);
    rowBtns->addWidget(btnAddRow);
    rowBtns->addWidget(btnRemoveRow);
    rowBtns->addStretch();
    rowBtns->addWidget(btnJoinType);
    rowBtns->addWidget(btnJunction);

    // --- integridad referencial ---
    auto *chkIntegrity = new QCheckBox("Exigir integridad referencial");
    auto *chkUpdate    = new QCheckBox("Actualizar en cascada los campos relacionados");
    auto *chkDelete    = new QCheckBox("Eliminar en cascada los registros relacionados");
    chkUpdate->setEnabled(false);
    chkDelete->setEnabled(false);
    QObject::connect(chkIntegrity, &QCheckBox::toggled, [&](bool on){
        chkUpdate->setEnabled(on);
        chkDelete->setEnabled(on);
    });

    // --- tipo de relación ---
    auto *lblType = new QLabel("Tipo de relación: Indeterminado");
    lblType->setStyleSheet("color:#444; font-weight:bold;");

    // Descripción extra
    auto *lblDesc = new QLabel("Seleccione campos para ver la descripción.");
    lblDesc->setStyleSheet("color:#666; font-size:11px;");
    lblDesc->setWordWrap(true);

    // selector manual
    auto *forceTypeLbl = new QLabel("Forzar tipo:");
    auto *cbForceType  = new QComboBox;
    cbForceType->addItem("Auto — Detectar automáticamente", "Auto");
    cbForceType->addItem("1:N — Uno a varios", "1:N");
    cbForceType->addItem("1:1 — Uno a uno", "1:1");
    cbForceType->addItem("N:M — Varios a varios", "N:M");
    cbForceType->setToolTip("Elige un tipo explícito o deja 'Auto' para deducir automáticamente");

    auto *forceRow = new QHBoxLayout;
    forceRow->addWidget(forceTypeLbl);
    forceRow->addWidget(cbForceType);
    forceRow->addStretch();


    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Aceptar");
    auto *btnOk = bb->button(QDialogButtonBox::Ok);
    btnOk->setEnabled(false);

    // ensamblar
    root->addWidget(top);
    midVL->addWidget(grid, 1);
    midVL->addLayout(rowBtns);
    root->addWidget(mid, 1);
    root->addWidget(chkIntegrity);
    root->addWidget(chkUpdate);
    root->addWidget(chkDelete);
    root->addSpacing(4);
    root->addLayout(forceRow);
    root->addWidget(lblType);
    root->addWidget(lblDesc);
    root->addWidget(bb);

    // ===== helpers =====
    auto normType = [](QString t) -> QString {
        // normaliza a minúsculas y quita espacios
        t = t.toLower().trimmed();

        // quitar acentos y unificar variantes comunes
        t.replace(QChar(u'á'), QChar(u'a'));
        t.replace(QChar(u'é'), QChar(u'e'));
        t.replace(QChar(u'í'), QChar(u'i'));
        t.replace(QChar(u'ó'), QChar(u'o'));
        t.replace(QChar(u'ú'), QChar(u'u'));

        // unificar "sí/no" -> "si/no"
        t.replace("sí/no", "si/no");

        // --- números enteros (PK/FK típicos) ---
        // Considera "autonumeracion" como entero para fines de compatibilidad
        if (t.contains("auto")) return "integer";  // autonumeracion / auto number
        if (t.contains("long integer") ||
            t.contains("entero") ||
            t.contains("integer") ||
            t.contains("numero")  ||   // "número"/"numero"
            t.contains("number"))
            return "integer";

        // --- reales/decimales ---
        if (t.contains("real") || t.contains("double") || t.contains("decimal") || t.contains("float"))
            return "real";

        // --- fechas/horas ---
        if (t.contains("fecha") || t.contains("hora") || t.contains("date") || t.contains("time"))
            return "datetime";

        // --- booleanos ---
        if (t.contains("bool") || t.contains("si/no"))
            return "bool";

        // --- textos ---
        if (t.contains("texto") || t.contains("text") || t.contains("char") || t.contains("varchar"))
            return "text";

        // por defecto, deja el literal tal cual
        return t;
    };

    auto setHeadersToTables = [&]{
        grid->horizontalHeaderItem(0)->setText(cbChildTable->currentText());
        grid->horizontalHeaderItem(1)->setText(cbParentTable->currentText());
    };

    auto makeFieldCombo = [&](const Schema& s)->QComboBox*{
        auto *cb = new QComboBox;
        for (const auto& f : s) {
            QString label = f.pk ? QString::fromUtf8("🔑 ") + f.name : f.name;
            cb->addItem(label, f.name);
        }
        return cb;
    };

    std::function<void()> recomputeState; // forward-declare

    auto currentPairs = [&]{
        QVector<QPair<QString,QString>> v;
        for (int r=0; r<grid->rowCount(); ++r) {
            auto *cbC = qobject_cast<QComboBox*>(grid->cellWidget(r,0));
            auto *cbP = qobject_cast<QComboBox*>(grid->cellWidget(r,1));
            if (!cbC || !cbP) continue;
            const QString cf = cbC->currentData().toString();
            const QString pf = cbP->currentData().toString();
            if (!cf.isEmpty() && !pf.isEmpty()) v.push_back({cf,pf});
        }
        return v;
    };

    auto fieldsAreUnique = [&](const QString& tableName, const QVector<QString>& fields)->bool {
        const Schema s = dm.schema(tableName);
        if (fields.isEmpty()) return false;
        bool allPk = true;
        for (const auto& fn : fields) {
            bool isPk = false;
            for (const auto& f : s) if (f.name==fn) { isPk = f.pk; break; }
            if (!isPk) { allPk = false; break; }
        }
        if (allPk) return true;
        for (const auto& fn : fields) {
            bool ok = false;
            for (const auto& f : s) if (f.name==fn) {
                    ok = f.indexado.contains("sin duplicados", Qt::CaseInsensitive);
                    break;
                }
            if (!ok) return false;
        }
        return true;
    };

    auto deduceRelType = [&](const QString& childTable, const QVector<QString>& childFields,
                             const QString& parentTable, const QVector<QString>& parentFields)->QString {
        const bool parentUnique = fieldsAreUnique(parentTable, parentFields);
        const bool childUnique  = fieldsAreUnique(childTable, childFields);
        if (parentUnique && !childUnique) return "1:N";
        if (parentUnique &&  childUnique) return "1:1";
        return "N:M";
    };

    auto typesCompatible = [&](const QString& cField, const QString& pField)->bool{
        const Schema sc = dm.schema(cbChildTable->currentText());
        const Schema sp = dm.schema(cbParentTable->currentText());
        auto findF = [](const Schema& s, const QString& n)->FieldDef{
            for (const auto& f : s) if (f.name==n) return f; return FieldDef{};
        };
        const auto cf = findF(sc, cField);
        const auto pf = findF(sp, pField);
        return normType(cf.type) == normType(pf.type);
    };

    auto addRow = [&]{
        const Schema childS  = dm.schema(cbChildTable->currentText());
        const Schema parentS = dm.schema(cbParentTable->currentText());
        if (childS.isEmpty() || parentS.isEmpty()) return;
        int r = grid->rowCount();
        grid->insertRow(r);
        auto *cbChild  = makeFieldCombo(childS);
        auto *cbParent = makeFieldCombo(parentS);
        grid->setCellWidget(r, 0, cbChild);
        grid->setCellWidget(r, 1, cbParent);
        grid->selectRow(r);
        QObject::connect(cbChild,  qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [&](int){ recomputeState(); });
        QObject::connect(cbParent, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [&](int){ recomputeState(); });
    };

    // --- recomputeState ---
    recomputeState = [&]{
        setHeadersToTables();
        const auto pairs = currentPairs();
        if (pairs.isEmpty()) {
            lblType->setText("Tipo de relación: Indeterminado");
            lblDesc->setText("Seleccione al menos un par de campos compatibles para deducir el tipo de relación.");
            btnOk->setEnabled(false);
            btnJunction->setEnabled(false);
            return;
        }
        for (const auto& pr : pairs) {
            if (!typesCompatible(pr.first, pr.second)) {
                lblType->setText("Tipo de relación: tipos incompatibles");
                lblDesc->setText("Los campos seleccionados no son compatibles en tipo de datos.");
                btnOk->setEnabled(false);
                btnJunction->setEnabled(false);
                return;
            }
        }
        QVector<QString> childFields, parentFields;
        for (const auto& pr : pairs) { childFields << pr.first; parentFields << pr.second; }
        const QString autoType = deduceRelType(cbChildTable->currentText(), childFields,
                                               cbParentTable->currentText(), parentFields);

        const QString forced = cbForceType->currentData().toString(); // valor real

        auto applyUiFor = [&](const QString& t, const QString& hint){
            if (t == "1:N") {
                lblType->setText(hint.isEmpty() ? "Tipo de relación: Uno a varios (1:N)"
                                                : hint + " — Uno a varios (1:N)");
                lblDesc->setText("Cada registro en la tabla padre puede relacionarse con varios registros en la tabla hija.");
                btnOk->setEnabled(true);
                btnJunction->setEnabled(false);

            } else if (t == "1:1") {
                lblType->setText(hint.isEmpty() ? "Tipo de relación: Uno a uno (1:1)"
                                                : hint + " — Uno a uno (1:1)");
                lblDesc->setText("Cada registro en la tabla padre se relaciona con un único registro en la tabla hija.");
                btnOk->setEnabled(true);
                btnJunction->setEnabled(false);

            } else if (t == "N:M") {
                lblType->setText(hint.isEmpty() ? "Tipo de relación: Varios a varios (N:M) — requiere tabla de unión"
                                                : hint + " — N:M (requiere tabla de unión)");
                lblDesc->setText("Cada registro en una tabla puede relacionarse con varios registros de la otra y viceversa. Requiere una tabla de unión.");
                btnOk->setEnabled(false);
                btnJunction->setEnabled(true);

            } else {
                lblType->setText("Tipo de relación: Indeterminado");
                lblDesc->setText("Seleccione al menos un par de campos compatibles para deducir el tipo de relación.");
                btnOk->setEnabled(false);
                btnJunction->setEnabled(false);
            }
        };

        if (forced == "Auto") {
            applyUiFor(autoType, "");
        } else if (forced == "1:N") {
            applyUiFor("1:N", "(forzado)");
        } else if (forced == "1:1") {
            applyUiFor("1:1", "(forzado)");
        } else if (forced == "N:M") {
            applyUiFor("N:M", "(forzado)");
        }
    };

    auto clearGrid = [&]{ grid->setRowCount(0); recomputeState(); };

    addRow();
    recomputeState();

    // eventos
    QObject::connect(cbChildTable,  &QComboBox::currentTextChanged, [&](const QString&){ clearGrid(); addRow(); recomputeState(); });
    QObject::connect(cbParentTable, &QComboBox::currentTextChanged, [&](const QString&){ clearGrid(); addRow(); recomputeState(); });
    QObject::connect(btnAddRow, &QPushButton::clicked, [&]{ addRow(); recomputeState(); });
    QObject::connect(btnRemoveRow, &QPushButton::clicked, [&]{ int r = grid->currentRow(); if (r>=0) grid->removeRow(r); recomputeState(); });
    QObject::connect(grid, &QTableWidget::itemSelectionChanged, [&]{ recomputeState(); });
    QObject::connect(cbForceType, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [&](int){ recomputeState(); });
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // === aceptar ===
    QObject::connect(bb, &QDialogButtonBox::accepted, [&]{
        const auto pairs = currentPairs();
        if (pairs.isEmpty()) { QMessageBox::warning(&dlg, "Relación", "Agrega al menos un par de campos."); return; }
        const QString childT  = cbChildTable->currentText();
        const QString parentT = cbParentTable->currentText();

        QVector<QString> cf, pf; for (auto& pr : pairs) { cf<<pr.first; pf<<pr.second; }
        const QString autoKind = deduceRelType(childT, cf, parentT, pf);
        const QString forced = cbForceType->currentText();
        QString eff = (forced == "Auto") ? autoKind : forced;

        if (eff == "N:M") {
            QMessageBox::warning(&dlg, "Relación", "Has elegido N:M. Usa el botón 'Crear tabla de unión…'.");
            return;
        }

        FkAction onDel = FkAction::Restrict;
        FkAction onUpd = FkAction::Restrict;
        if (chkIntegrity->isChecked()) {
            if (chkDelete->isChecked()) onDel = FkAction::Cascade;
            if (chkUpdate->isChecked()) onUpd = FkAction::Cascade;
        }

        for (const auto& pr : pairs) {
            QString err;
            if (!dm.addRelationship(childT, pr.first, parentT, pr.second, onDel, onUpd, &err)) {
                QMessageBox::warning(&dlg, "Relación", err);
                return;
            }
        }
        dlg.accept();
    });

    if (dlg.exec() != QDialog::Accepted) return false;

    QMessageBox::information(parent, "Relaciones", "Relación(es) creada(s) correctamente.");
    return true;
}




// --- Panel izquierdo tipo Access (no usado directamente, helper por si se necesita) ---
static QWidget* buildLeftPanel(int width, int height) {
    auto *panel = new QWidget;
    panel->setFixedSize(width, height);
    panel->setStyleSheet("background:white; border-right:1px solid #b0b0b0;");

    auto *v = new QVBoxLayout(panel);
    v->setContentsMargins(0,0,0,0);
    v->setSpacing(6);

    auto *hdr = new QLabel("Tablas");
    hdr->setFixedHeight(32);
    hdr->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    hdr->setStyleSheet("background:#9f3639; color:white; font-weight:bold; font-size:20px; padding:6px 8px;");

    auto *search = new QLineEdit;
    search->setFocusPolicy(Qt::ClickFocus);
    search->setPlaceholderText("Search");
    search->setFixedHeight(26);
    search->setClearButtonEnabled(true);
    search->setStyleSheet("QLineEdit{border:1px solid #c8c8c8; padding:2px 6px; margin:0 6px;}");
    search->addAction(qApp->style()->standardIcon(QStyle::SP_FileDialogContentsView), QLineEdit::LeadingPosition);

    auto *list = new QListWidget;
    list->setFrameShape(QFrame::NoFrame);
    list->setIconSize(QSize(18,18));
    list->setStyleSheet(
        "QListWidget{border:none;}"
        "QListWidget::item{padding:6px 8px;}"
        "QListWidget::item:selected{background:#f4c9cc;}"
        );
    list->addItem(new QListWidgetItem(
        qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        "Table1"
        ));

    v->addWidget(hdr);
    v->addWidget(search);
    v->addWidget(list, 1);
    return panel;
}

// --- Barra de navegación tipo Access (1140x36) ---
// (Conexiones hacia RecordsPage mediante invokeMethod para no depender de su API pública)
static QWidget* buildRecordNavigator(int width, int height, RecordsPage* recordsPage) {
    auto *bar = new QWidget;
    bar->setFixedSize(width, height);
    bar->setStyleSheet("background:#f3f3f3; border-top:1px solid #c8c8c8;");

    auto *hl = new QHBoxLayout(bar);
    hl->setContentsMargins(8,4,8,4);
    hl->setSpacing(10);

    auto *recLbl = new QLabel("Record:");
    recLbl->setStyleSheet("color:#444; background:transparent; border:none;");

    auto makeNav = [](QStyle::StandardPixmap sp, bool enabled=true, const char* objName=nullptr){
        auto *b = new QToolButton;
        b->setAutoRaise(false);
        b->setIcon(qApp->style()->standardIcon(sp));
        b->setEnabled(enabled);
        b->setStyleSheet("QToolButton { border:none; background:transparent; }");
        if (objName) b->setObjectName(objName);
        return b;
    };
    auto *firstBtn = makeNav(QStyle::SP_MediaSkipBackward, true,  "btnFirst");
    auto *prevBtn  = makeNav(QStyle::SP_ArrowBack,        true,  "btnPrev");
    auto *nextBtn  = makeNav(QStyle::SP_ArrowForward,     true,  "btnNext");
    auto *lastBtn  = makeNav(QStyle::SP_MediaSkipForward, true,  "btnLast");

    auto *pos = new QLabel("1 of 1");
    pos->setObjectName("lblPos");
    pos->setStyleSheet("background:transparent; border:none; color:#333; padding:2px;");

    auto *filterBtn = new QToolButton;
    filterBtn->setObjectName("btnFilter");
    filterBtn->setCheckable(true);
    filterBtn->setAutoRaise(true);
    filterBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_DialogCancelButton)); // off
    filterBtn->setStyleSheet("QToolButton { border:none; background:transparent; }");

    auto *filterLbl = new QLabel("No Filter");
    filterLbl->setObjectName("lblFilter");
    filterLbl->setStyleSheet("color:#888; background:transparent; border:none; min-width:70px;");

    QObject::connect(filterBtn, &QToolButton::toggled, [filterBtn, filterLbl](bool on){
        if (on) {
            filterBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_DialogYesButton));
            filterLbl->setText("Filter On");
            filterLbl->setStyleSheet("color:#006400; font-weight:bold; background:transparent; border:none; min-width:70px;");
        } else {
            filterBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_DialogCancelButton));
            filterLbl->setText("No Filter");
            filterLbl->setStyleSheet("color:#888; background:transparent; border:none; min-width:70px;");
        }
    });

    auto *search = new QLineEdit;
    search->setFocusPolicy(Qt::ClickFocus);
    search->setObjectName("navSearch");
    search->setPlaceholderText("Search");
    search->setFixedWidth(240);
    search->setStyleSheet("background:white; border:1px solid #c8c8c8; padding:2px 6px;");
    search->setFocusPolicy(Qt::ClickFocus);


    // Conexiones a RecordsPage (usamos invokeMethod con fallback a paginación mock)
    QObject::connect(firstBtn, &QToolButton::clicked, recordsPage, [recordsPage]{
        bool ok = QMetaObject::invokeMethod(recordsPage, "navFirst", Qt::DirectConnection);
        if (!ok) QMetaObject::invokeMethod(recordsPage, "onPrimero", Qt::DirectConnection);
    });
    QObject::connect(prevBtn, &QToolButton::clicked, recordsPage, [recordsPage]{
        bool ok = QMetaObject::invokeMethod(recordsPage, "navPrev", Qt::DirectConnection);
        if (!ok) QMetaObject::invokeMethod(recordsPage, "onAnterior", Qt::DirectConnection);
    });
    QObject::connect(nextBtn, &QToolButton::clicked, recordsPage, [recordsPage]{
        bool ok = QMetaObject::invokeMethod(recordsPage, "navNext", Qt::DirectConnection);
        if (!ok) QMetaObject::invokeMethod(recordsPage, "onSiguiente", Qt::DirectConnection);
    });
    QObject::connect(lastBtn, &QToolButton::clicked, recordsPage, [recordsPage]{
        bool ok = QMetaObject::invokeMethod(recordsPage, "navLast", Qt::DirectConnection);
        if (!ok) QMetaObject::invokeMethod(recordsPage, "onUltimo", Qt::DirectConnection);
    });
    QObject::connect(search, &QLineEdit::textChanged, recordsPage, [recordsPage](const QString& t){
        // equivalente a escribir en la caja de búsqueda de RecordsPage
        QMetaObject::invokeMethod(recordsPage, "onBuscarChanged", Qt::DirectConnection,
                                  Q_ARG(QString, t));
    });

    hl->addWidget(recLbl);
    hl->addWidget(firstBtn);
    hl->addWidget(prevBtn);
    hl->addWidget(pos);
    hl->addWidget(nextBtn);
    hl->addWidget(lastBtn);

    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet("color:#c8c8c8;");
    hl->addSpacing(6);
    hl->addWidget(sep);
    hl->addSpacing(6);

    hl->addWidget(filterBtn);
    hl->addWidget(filterLbl);
    hl->addSpacing(20);
    hl->addWidget(search);
    hl->addStretch();

    return bar;
}

// ----------------- helpers de estilo -----------------
static QToolButton* makeTabBtn(const QString& text) {
    auto *btn = new QToolButton;
    btn->setText(text);
    btn->setCheckable(true);
    btn->setAutoRaise(false);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setFixedHeight(32);
    btn->setStyleSheet(R"(
        QToolButton { color:white; background:transparent; border:none;
                      padding:6px 12px; font-size:14px; font-weight:600; }
        QToolButton:hover  { background:#8d2f33; border-radius:4px; }
        QToolButton:checked{ background:#7f2b2e; border-radius:4px; }
    )");
    return btn;
}

static QToolButton* makeActionBtn(const QString& text, const QString& iconPath = QString()) {
    auto *b = new QToolButton;
    QIcon icon;

    if (!iconPath.isEmpty()) {
        icon = QIcon(iconPath);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIconSize(QSize(24,24));
    } else {
        if (text.contains("New", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_FileIcon);
        else if (text.contains("Save", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_DialogSaveButton);
        else if (text.contains("Delete", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_TrashIcon);
        else if (text.contains("Find", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_FileDialogContentsView);
        else if (text.contains("Sort Asc", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_ArrowUp);
        else if (text.contains("Sort Desc", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_ArrowDown);
        else if (text.contains("Filter", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_DirIcon);
        else if (text.contains("Clear", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_DialogResetButton);
        else if (text.contains("Datasheet", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView);
        else if (text.contains("Design", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_FileDialogListView);
        else
            icon = qApp->style()->standardIcon(QStyle::SP_FileIcon);

        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIconSize(QSize(24,24));
    }

    b->setIcon(icon);
    b->setText(text);
    b->setAutoRaise(false);
    b->setCheckable(false);
    b->setFocusPolicy(Qt::NoFocus);
    b->setStyleSheet(R"(
        QToolButton { padding:6px 10px; }
        QToolButton:hover { background:#efefef; border-radius:4px; }
    )");

    return b;
}

// ——— Ribbon group: TÍTULO ABAJO (como Access) ———
static QWidget* makeRibbonGroup(const QString& title, const QList<QToolButton*>& buttons) {
    auto *wrap = new QWidget;
    wrap->setAttribute(Qt::WA_StyledBackground, true);
    wrap->setStyleSheet("background:transparent; border:none;");
    auto *vl = new QVBoxLayout(wrap);
    vl->setContentsMargins(6,6,6,0);
    vl->setSpacing(0);

    // fila de botones
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0,0,0,0);
    row->setSpacing(8);
    row->addStretch();
    for (auto *btn : buttons) row->addWidget(btn);
    row->addStretch();

    auto *frame = new QWidget;
    frame->setAttribute(Qt::WA_StyledBackground, true);
    frame->setStyleSheet("background:transparent; border:none;");
    auto *frameLay = new QVBoxLayout(frame);
    frameLay->setContentsMargins(8,6,8,6);
    frameLay->setSpacing(0);
    frameLay->addLayout(row);

    // título ABAJO
    auto *cap = new QLabel(title);
    cap->setAlignment(Qt::AlignHCenter);
    cap->setContentsMargins(0,2,0,0);
    cap->setStyleSheet("font-size:11px; color:#666; background:transparent; border:none;");

    vl->addWidget(frame);
    vl->addWidget(cap);
    return wrap;
}

// ===============================================
// ====== Diagrama de Relaciones + Sidebar =======
// ===============================================
class TableNode : public QGraphicsObject {
    Q_OBJECT
public:
    TableNode(const QString& t, const Schema& s, QGraphicsItem* parent=nullptr)
        : QGraphicsObject(parent), table_(t), schema_(s) {
        setFlag(ItemIsMovable, true);
        setFlag(ItemSendsGeometryChanges, true);
        setCacheMode(DeviceCoordinateCache);

        QFont ft; ft.setBold(true);
        int w = QFontMetrics(ft).horizontalAdvance(table_) + 24;
        QFontMetrics fm(QApplication::font());
        for (const auto& col : schema_) w = qMax(w, fm.horizontalAdvance(col.name) + 40);
        int h = int(headerH_ + rowH_ * qMax(1, schema_.size()));
        rect_ = QRectF(0,0, qMax(220, w), h);
    }

    QString tableName() const { return table_; }
    QPointF portScenePos(int i) const {
        const qreal y = headerH_ + rowH_*(i + 0.5);
        const qreal x = rect_.right();
        return mapToScene(QPointF(x, y));
    }
    QRectF boundingRect() const override { return rect_; }



signals:
    void moved();
    void requestNewRelation(const QString& table);

protected:
    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        p->setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect_;

        // cuerpo
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255,255,240));
        p->drawRoundedRect(r, 8, 8);

        // header
        p->setBrush(QColor(255,242,204));
        p->drawRoundedRect(QRectF(r.left(), r.top(), r.width(), headerH_), 8, 8);
        p->setPen(QPen(QColor(214,214,214), 1));
        p->drawLine(QPointF(r.left(), headerH_), QPointF(r.right(), headerH_));

        // título
        QFont f = p->font(); f.setBold(true); p->setFont(f);
        p->setPen(QColor(60,60,60));
        p->drawText(QRectF(r.left()+8, r.top(), r.width()-16, headerH_), Qt::AlignVCenter|Qt::AlignLeft, table_);

        // columnas
        f.setBold(false); p->setFont(f);
        for (int i=0;i<schema_.size();++i) {
            const auto& col = schema_[i];
            if (col.pk) { QFont fb=p->font(); fb.setBold(true); p->setFont(fb); p->setPen(QColor(80,40,40)); }
            else         { QFont fn=p->font(); fn.setBold(false); p->setFont(fn); p->setPen(QColor(40,40,40)); }

            p->drawText(QRectF(r.left()+8, headerH_ + rowH_*i, r.width()-16, rowH_),
                        Qt::AlignVCenter|Qt::AlignLeft,
                        (col.pk ? QString::fromUtf8("🔑 ") : "  ") + col.name);

            // punto de conexión
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(200,200,200));
            p->drawEllipse(QPointF(r.right()-6, headerH_ + rowH_*i + rowH_/2), 3.5, 3.5);
        }

        // borde
        p->setPen(QPen(QColor(214,214,214), 1));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(r, 8, 8);
    }

    QVariant itemChange(GraphicsItemChange ch, const QVariant& v) override {
        if (ch == ItemPositionHasChanged) emit moved();
        return QGraphicsObject::itemChange(ch, v);
    }

protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* e) override {
        QMenu menu;
        QAction* newRel = menu.addAction("Nueva relación…");
        QAction* chosen = menu.exec(e->screenPos());
        if (chosen == newRel) {
            emit requestNewRelation(table_);  // avisar a RelationsPage
        }
    }


private:
    QString table_;
    Schema  schema_;
    QRectF  rect_;
    qreal   headerH_ = 28.0;
    qreal   rowH_    = 22.0;
};



class RelationEdge : public QGraphicsPathItem {
public:
    RelationEdge(TableNode* c, int cf,
                 TableNode* p, int pf,
                 const QString& tooltip,
                 const QString& relType,
                 FkAction /*onDelete*/, FkAction /*onUpdate*/)
        : QGraphicsPathItem(nullptr)
        , c_(c), cf_(cf), p_(p), pf_(pf), relType_(relType)
    {
        // Siempre sobre los nodos para que no se esconda
        setZValue(1);
        setAcceptHoverEvents(true);
        setFlag(ItemIsSelectable, true);

        basePen_  = QPen(Qt::black, 1.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        hoverPen_ = QPen(Qt::black, 1.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        selPen_   = QPen(Qt::black, 1.5, Qt::DashLine, Qt::SquareCap, Qt::MiterJoin);

        setPen(basePen_);
        setToolTip(tooltip);

        // Etiquetas "1"/"N" a cada extremo
        markChild_  = new QGraphicsSimpleTextItem(this);
        markParent_ = new QGraphicsSimpleTextItem(this);
        QFont f = markChild_->font();
        f.setBold(true);
        markChild_->setFont(f);
        markParent_->setFont(f);

        refresh();

        if (c_) QObject::connect(c_, &TableNode::moved, [this]{ refresh(); });
        if (p_) QObject::connect(p_, &TableNode::moved, [this]{ refresh(); });
    }

    void refresh() {
        if (!c_ || !p_) return;

        a_ = c_->portScenePos(cf_);
        b_ = p_->portScenePos(pf_);

        // Separaciones
        const qreal insetFromPorts = 8.0;  // separa el trazo de los puertos
        const qreal arrowGap       = 8.0;  // distancia entre punta de flecha y borde de tabla (no tocar)
        const qreal arrowLen       = 10.0; // largo de flecha

        QVector2D v(b_ - a_);
        if (v.length() < 1.0) return;
        QVector2D u = v.normalized();

        // Puntos internos del cable
        a1_ = a_ + u.toPointF()*insetFromPorts;
        b1_ = b_ - u.toPointF()*insetFromPorts;  // todavía “toca” el borde lógico; luego recortamos

        // Ruta ortogonal ┐└
        const qreal midX = (a1_.x() + b1_.x()) * 0.5;

        // Construimos la polilínea para conocer el último segmento
        QVector<QPointF> pts;
        pts << a1_
            << QPointF(midX, a1_.y())
            << QPointF(midX, b1_.y())
            << b1_;

        // Dirección del último segmento (hacia el padre)
        QVector2D lastDir(pts.last() - pts[pts.size()-2]);
        if (lastDir.length() < 0.1) lastDir = QVector2D(1,0);
        lastDir.normalize();

        // Definimos punta y base de flecha con GAP (no toca la tabla)
        arrowTip_  = pts.last() - lastDir.toPointF()*arrowGap;
        arrowBase_ = arrowTip_  - lastDir.toPointF()*arrowLen;

        // Reemplazamos el último punto del camino por la base (línea termina antes)
        pts.last() = arrowBase_;

        // Construimos path recto por segmentos
        QPainterPath path(pts.first());
        for (int i=1;i<pts.size();++i) path.lineTo(pts[i]);
        setPath(path);

        // Hitbox generosa
        QPainterPathStroker stroker; stroker.setWidth(10);
        hitShape_ = stroker.createStroke(path);
        hitShape_.addPath(path);

        // ====== Actualizar etiquetas "1"/"N" ======
        QString childText = "N", parentText = "1";
        if (relType_ == "1:1") { childText = "1"; parentText = "1"; }
        else if (relType_ == "N:M") { childText = "N"; parentText = "N"; }

        markChild_->setText(childText);
        markParent_->setText(parentText);

        // Vector normal en el primer y último tramo para separar el texto del cable
        QVector2D dirFirst((pts[1] - pts[0]));
        if (dirFirst.length() < 0.1) dirFirst = QVector2D(1,0);
        dirFirst.normalize();
        QVector2D nFirst(-dirFirst.y(), dirFirst.x());

        QVector2D dirLast(arrowTip_ - arrowBase_);
        if (dirLast.length() < 0.1) dirLast = QVector2D(1,0);
        dirLast.normalize();
        QVector2D nLast(-dirLast.y(), dirLast.x());

        const qreal off = 10.0; // separación lateral
        QPointF childPos  = a1_        + nFirst.toPointF()*off;  // cerca del extremo hijo
        QPointF parentPos = arrowBase_ + nLast.toPointF()*off;   // cerca del extremo padre (flecha)

        // Centrar visualmente el texto en su punto
        QRectF brC = markChild_->boundingRect();
        QRectF brP = markParent_->boundingRect();
        markChild_->setPos(childPos  - QPointF(brC.width()/2, brC.height()/2));
        markParent_->setPos(parentPos - QPointF(brP.width()/2, brP.height()/2));
    }

    QPainterPath shape() const override { return hitShape_; }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent*) override { setPen(hoverPen_); }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override { setPen(isSelected() ? selPen_ : basePen_); }
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override {
        QGraphicsPathItem::mousePressEvent(e); setPen(selPen_);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override {
        QGraphicsPathItem::mouseReleaseEvent(e); setPen(hoverPen_);
    }

    void paint(QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w) override {
        QGraphicsPathItem::paint(p, o, w);
        drawArrow(p);  // flecha separada de la tabla
        // (las etiquetas son QGraphicsItems hijos: no hace falta pintarlas aquí)
    }

private:
    void drawArrow(QPainter* p) const {
        // Triángulo sólido en (arrowTip_), orientado según el último segmento
        const QPainterPath& pa = path();
        if (pa.elementCount() < 2) return;

        QPointF prev(pa.elementAt(pa.elementCount()-1).x,
                     pa.elementAt(pa.elementCount()-1).y); // este es arrowBase_
        QVector2D dir(arrowTip_ - prev);
        if (dir.length() < 0.1) return;
        dir.normalize();

        QVector2D n(-dir.y(), dir.x());
        const qreal L = (arrowTip_ - prev).manhattanLength(); // largo actual (≈ arrowLen)
        const qreal W = 6.0;

        QPointF a = arrowTip_ - dir.toPointF()*L + n.toPointF()*W;
        QPointF b = arrowTip_;
        QPointF c = arrowTip_ - dir.toPointF()*L - n.toPointF()*W;

        QPen old = p->pen();
        p->setBrush(old.color());
        p->setPen(Qt::NoPen);
        p->drawPolygon(QPolygonF() << a << b << c);
        p->setPen(old);
    }

private:
    QPointer<TableNode> c_;
    int cf_;
    QPointer<TableNode> p_;
    int pf_;

    QPointF a_, b_;     // puertos exactos
    QPointF a1_, b1_;   // puntos internos (sin tocar los rects)
    QPointF arrowBase_; // donde termina la línea
    QPointF arrowTip_;  // punta (separada del rect por arrowGap)

    // NUEVO: tipo y etiquetas
    QString relType_;
    QGraphicsSimpleTextItem* markChild_  = nullptr;
    QGraphicsSimpleTextItem* markParent_ = nullptr;

    QPen basePen_, hoverPen_, selPen_;
    QPainterPath hitShape_;
};

// ===============================================
// ================ FORM PAGE ====================
// ===============================================
#include <QFormLayout>

class FormPage : public QWidget {
    Q_OBJECT
public:
    explicit FormPage(QWidget* parent=nullptr)
        : QWidget(parent)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(12,12,12,12);
        root->setSpacing(10);

        // --- barra superior ---
        auto *top = new QWidget;
        auto *th  = new QHBoxLayout(top);
        th->setContentsMargins(0,0,0,0);
        th->setSpacing(8);

        cbTable_ = new QComboBox;
        cbTable_->setMinimumWidth(260);
        th->addWidget(new QLabel("Seleccionar tabla:"));
        th->addWidget(cbTable_, 1);

        btnNew_    = new QPushButton("Nuevo");
        btnEdit_   = new QPushButton("Editar");
        btnDelete_ = new QPushButton("Eliminar");
        btnSave_   = new QPushButton("Guardar");
        btnSave_->setEnabled(false);

        for (auto *b : {btnNew_, btnEdit_, btnDelete_, btnSave_}) b->setFixedHeight(28);

        th->addSpacing(8);
        th->addWidget(btnNew_);
        th->addWidget(btnEdit_);
        th->addWidget(btnDelete_);
        th->addWidget(btnSave_);
        th->addStretch();
        root->addWidget(top);

        // --- indicador posición ---
        posLbl_ = new QLabel("Registro 0 de 0");
        posLbl_->setAlignment(Qt::AlignCenter);
        posLbl_->setStyleSheet("background:#eee; border:1px solid #ddd; padding:6px; font-weight:bold;");
        root->addWidget(posLbl_);

        // --- contenedor del formulario ---
        formWrap_ = new QWidget;
        formWrap_->setStyleSheet("background:#fce4e4; border:1px solid #e2baba; border-radius:8px;");
        auto *formWrapVL = new QVBoxLayout(formWrap_);
        formWrapVL->setContentsMargins(16,16,16,16);
        formWrapVL->setSpacing(12);

        form_ = new QFormLayout;
        form_->setLabelAlignment(Qt::AlignLeft);
        form_->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        formWrapVL->addLayout(form_);
        root->addWidget(formWrap_, 1);

        // Poblar combo
        cbTable_->addItems(DataModel::instance().tables());

        // Conexiones
        connect(cbTable_, &QComboBox::currentTextChanged, this, [this](const QString& t){ loadTable(t); });
        connect(btnNew_,  &QPushButton::clicked, this, [this]{ clearEditors(); mode_ = Mode::New;  btnSave_->setEnabled(true); });
        connect(btnEdit_, &QPushButton::clicked, this, [this]{ loadCurrentRowIntoEditors(); mode_ = Mode::Edit; btnSave_->setEnabled(true); });
        connect(btnDelete_, &QPushButton::clicked, this, [this]{ deleteCurrentRow(); });
        connect(btnSave_, &QPushButton::clicked, this, [this]{ onSave(); });

        if (cbTable_->count() > 0) loadTable(cbTable_->currentText());
    }

    // la usa ShellWindow para sincronizar cuando eligen una tabla
    void setTable(const QString& t, const Schema&) {
        int ix = cbTable_->findText(t);
        if (ix < 0) { cbTable_->addItem(t); ix = cbTable_->findText(t); }
        cbTable_->setCurrentIndex(ix);
    }

private:
    enum class Mode { View, New, Edit };

    static QString normType(const QString& tIn) {
        QString t = tIn.toLower().trimmed();
        if (t.contains("auto")) return "autonumeracion";
        if (t.contains("entero") || t.contains("number") || t=="número" || t=="numero") return "number";
        if (t.contains("fecha") || t.contains("hora") || t.contains("date")) return "datetime";
        if (t.contains("bool")  || t.contains("sí/no") || t.contains("si/no")) return "bool";
        if (t.contains("real")  || t.contains("double") || t.contains("decimal")) return "real";
        return "text";
    }

    QWidget* makeEditor(const FieldDef& f) {
        const QString nt = normType(f.type);
        if (nt == "autonumeracion") {
            auto *ed = new QLineEdit; ed->setReadOnly(true);
            ed->setPlaceholderText("Autonumeración"); ed->setStyleSheet("background:#fafafa;"); return ed;
        } else if (nt == "number") {
            auto *ed = new QLineEdit; ed->setPlaceholderText("Solo enteros");
            ed->setValidator(new QIntValidator(std::numeric_limits<int>::min(),
                                               std::numeric_limits<int>::max(), ed)); return ed;
        } else if (nt == "real") {
            auto *ed = new QLineEdit; ed->setPlaceholderText("Número (decimal)");
            ed->setValidator(new QDoubleValidator(ed)); return ed;
        } else if (nt == "datetime") {
            auto *ed = new QDateTimeEdit(QDateTime::currentDateTime()); ed->setCalendarPopup(true); return ed;
        } else if (nt == "bool") {
            return new QCheckBox;
        } else {
            auto *ed = new QLineEdit; ed->setPlaceholderText("Texto"); return ed;
        }
    }

    QVariant readEditor(QWidget* w, const FieldDef& f) const {
        const QString nt = normType(f.type);
        if (nt == "autonumeracion") return QVariant();
        if (nt == "number" || nt=="real") return qobject_cast<QLineEdit*>(w)->text().trimmed();
        if (nt == "datetime") return qobject_cast<QDateTimeEdit*>(w)->dateTime();
        if (nt == "bool") return qobject_cast<QCheckBox*>(w)->isChecked();
        return qobject_cast<QLineEdit*>(w)->text();
    }

    void setEditor(QWidget* w, const FieldDef& f, const QVariant& v) {
        const QString nt = normType(f.type);
        if (nt == "autonumeracion") qobject_cast<QLineEdit*>(w)->setText(v.toString());
        else if (nt == "number" || nt == "real") qobject_cast<QLineEdit*>(w)->setText(v.toString());
        else if (nt == "datetime") qobject_cast<QDateTimeEdit*>(w)->setDateTime(v.isNull()? QDateTime::currentDateTime() : v.toDateTime());
        else if (nt == "bool") qobject_cast<QCheckBox*>(w)->setChecked(v.toBool());
        else qobject_cast<QLineEdit*>(w)->setText(v.toString());
    }

    void buildEditors() {
        for (auto *w : editors_) w->deleteLater();
        editors_.clear();
        while (QLayoutItem* it = form_->takeAt(0)) delete it;

        const QFont labelFont = font();
        for (const auto& f : schema_) {
            auto *ed = makeEditor(f);
            editors_.push_back(ed);
            auto *lbl = new QLabel(f.name);
            lbl->setStyleSheet("color:#333;");
            QFont lf = labelFont; lf.setCapitalization(QFont::AllLowercase);
            lbl->setFont(lf);
            form_->addRow(lbl, ed);
        }
    }

    void clearEditors() {
        for (int i=0;i<schema_.size();++i) setEditor(editors_[i], schema_[i], QVariant());
    }

    void loadTable(const QString& t) {
        table_ = t;
        schema_ = DataModel::instance().schema(t);
        buildEditors();
        current_ = 0;
        total_ = DataModel::instance().rowCount(t);
        updatePos();
        mode_ = Mode::View;
        btnSave_->setEnabled(false);
    }

    void updatePos() {
        posLbl_->setText(QString("Registro %1 de %2").arg(total_==0?0:qMin(current_+1,total_)).arg(total_));
    }

    void loadCurrentRowIntoEditors() {
        if (table_.isEmpty() || total_==0) { clearEditors(); return; }

        // Tu DataModel expone rows(table) -> lista de Record
        const auto recs = DataModel::instance().rows(table_);
        if (current_ < 0 || current_ >= recs.size()) { clearEditors(); return; }

        const auto rec = recs.value(current_); // value() evita crash si hay out-of-range
        for (int i=0; i<schema_.size() && i<rec.size(); ++i) {
            setEditor(editors_[i], schema_[i], rec[i]);
        }
    }


    void deleteCurrentRow() {
        if (table_.isEmpty() || total_==0) return;
        QString err;
        if (!DataModel::instance().removeRows(table_, {current_}, &err)) {
            QMessageBox::warning(this, tr("Eliminar"), err); return;
        }
        total_ = DataModel::instance().rowCount(table_);
        if (current_ >= total_) current_ = qMax(0, total_-1);
        updatePos();
        loadCurrentRowIntoEditors();
        btnSave_->setEnabled(false);
        mode_ = Mode::View;
    }

    void onSave() {
        if (table_.isEmpty()) return;
        Record r(schema_.size());
        for (int i=0;i<schema_.size(); ++i) r[i] = readEditor(editors_[i], schema_[i]);

        QString err;
        if (mode_ == Mode::New) {
            if (!DataModel::instance().insertRow(table_, r, &err)) {
                QMessageBox::warning(this, tr("Guardar"), err); return;
            }
            total_ = DataModel::instance().rowCount(table_);
            current_ = total_>0 ? total_-1 : 0;
            updatePos();
            mode_ = Mode::View;
            btnSave_->setEnabled(false);
            QMessageBox::information(this, tr("Guardar"), tr("Registro insertado."));
        } else if (mode_ == Mode::Edit) {
            if (!DataModel::instance().updateRow(table_, current_, r, &err)) {
                QMessageBox::warning(this, tr("Guardar"), err); return;
            }
            QMessageBox::information(this, tr("Guardar"), tr("Registro actualizado."));
            mode_ = Mode::View;
            btnSave_->setEnabled(false);
        }
    }

private:
    // UI
    QComboBox* cbTable_ = nullptr;
    QPushButton *btnNew_=nullptr, *btnEdit_=nullptr, *btnDelete_=nullptr, *btnSave_=nullptr;
    QLabel* posLbl_ = nullptr;
    QWidget* formWrap_ = nullptr;
    QFormLayout* form_ = nullptr;

    // Estado
    QString table_;
    Schema  schema_;
    QVector<QWidget*> editors_;
    int current_ = 0;
    int total_   = 0;
    Mode mode_   = Mode::View;
};


class RelationsPage : public QWidget {
    Q_OBJECT
public:
    explicit RelationsPage(QWidget* parent=nullptr)
        : QWidget(parent)
        , scene_(new QGraphicsScene(this))
        , view_(new QGraphicsView(scene_))
        , right_(new QWidget)
        , list_(new QListWidget)
        , btnAdd_(new QPushButton("Agregar la tabla seleccionada"))
        , btnNewRel_(new QPushButton("Nueva relación…"))
    {
        auto *hl = new QHBoxLayout(this);
        hl->setContentsMargins(0,0,0,0);
        hl->setSpacing(0);

        // View central (canvas)
        view_->setRenderHint(QPainter::Antialiasing, true);
        view_->setDragMode(QGraphicsView::RubberBandDrag);
        view_->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
        view_->setStyleSheet("background:white;");  // ← fondo blanco como Access
        hl->addWidget(view_, 1);

        // Panel derecho
        right_->setFixedWidth(220);
        auto *rv = new QVBoxLayout(right_);
        rv->setContentsMargins(8,8,8,8);
        rv->setSpacing(8);
        auto *hdr = new QLabel("Agregar tablas");
        hdr->setStyleSheet("font-weight:bold; color:#444;");
        list_->setSelectionMode(QAbstractItemView::SingleSelection);
        rv->addWidget(hdr);
        rv->addWidget(list_, 1);
        rv->addWidget(btnAdd_);
        rv->addWidget(btnNewRel_);
        hl->addWidget(right_);

        refreshSidebar();

        connect(btnAdd_, &QPushButton::clicked, this, [this]{
            if (auto *it = list_->currentItem()) addTableNode(it->text());
        });
        connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it){
            if (it) addTableNode(it->text());
        });

        // CLICK simple también agrega la tabla al canvas
        connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it){
            if (it) addTableNode(it->text());
        });

        connect(btnNewRel_, &QPushButton::clicked, this, [this]{
            emit requestAddRelation();  // lo maneja ShellWindow
        });


    }
public slots:
    void refreshSidebarFromModel();


    // asegura que el nodo de la tabla esté en el canvas (si no, lo agrega)
    void ensureNodeVisible(const QString& table) {
        if (!nodes_.contains(table)) addTableNode(table);
    }


    // Deja el canvas en blanco (como Access al abrir Relaciones)
    void startBlank() {
        for (RelationEdge* e : edges_) {
            if (!e) continue;
            scene_->removeItem(static_cast<QGraphicsItem*>(e));
            delete e;
        }
        edges_.clear();
        for (auto n : nodes_) {
            if (!n) continue;
            scene_->removeItem(n);
            delete n;
        }
        nodes_.clear();
        scene_->setSceneRect(QRectF(0,0,1000,700));
    }

    void addRelationEdge(const QString& childTable, int childCol,
                         const QString& parentTable, int parentCol,
                         FkAction onDelete, FkAction onUpdate)
    {
        TableNode* cnode = nodes_.value(childTable, nullptr);
        TableNode* pnode = nodes_.value(parentTable, nullptr);
        if (!cnode || !pnode) return;

        const Schema sc = DataModel::instance().schema(childTable);
        const Schema sp = DataModel::instance().schema(parentTable);
        if (childCol < 0 || parentCol < 0 || childCol >= sc.size() || parentCol >= sp.size()) return;

        auto actionToText = [](FkAction a){
            switch(a){ case FkAction::Restrict: return "Restrict";
            case FkAction::Cascade:  return "Cascade";
            case FkAction::SetNull:  return "SetNull"; }
            return "Restrict";
        };

        const QString tip = QString("%1.%2 → %3.%4\nON DELETE %5 | ON UPDATE %6")
                                .arg(childTable, sc[childCol].name,
                                     parentTable, sp[parentCol].name,
                                     actionToText(onDelete),
                                     actionToText(onUpdate));

        // === calcular cardinalidad para dibujar 1/∞ ===
        auto isUnique = [&](const QString& table, int col)->bool {
            const Schema s = DataModel::instance().schema(table);
            if (col < 0 || col >= s.size()) return false;
            const auto& f = s[col];
            if (f.pk) return true;
            return f.indexado.contains("sin duplicados", Qt::CaseInsensitive);
        };
        const bool parentUnique = isUnique(parentTable, parentCol);
        const bool childUnique  = isUnique(childTable, childCol);
        QString relType = "N:M";
        if (parentUnique && !childUnique) relType = "1:N";
        else if (parentUnique && childUnique) relType = "1:1";

        // === usar la nueva arista “bonita” ===
        auto* e = new RelationEdge(cnode, childCol, pnode, parentCol, tip, relType, onDelete, onUpdate);
        scene_->addItem(e);
        edges_.push_back(e);
    }

    // Redibuja solo las aristas (se espera que los nodos ya estén en canvas)
    void rebuildFromModel() { redrawAll(true); }



signals:
    void requestAddRelation(const QString& tableHint = QString());


private:
    QGraphicsScene* scene_;
    QGraphicsView*  view_;
    QWidget*        right_;
    QListWidget*    list_;
    QPushButton*    btnAdd_;
    QPushButton*    btnNewRel_;
    QMap<QString, TableNode*> nodes_;
    QList<RelationEdge*> edges_;

    void refreshSidebar() {
        list_->clear();
        for (const auto& t : DataModel::instance().tables())
            list_->addItem(t);
    }


    void addTableNode(const QString& table) {
        if (nodes_.contains(table)) return;

        auto* n = new TableNode(table, DataModel::instance().schema(table));
        connect(n, &TableNode::requestNewRelation, this, [this](const QString& table){
            emit requestAddRelation(table);  // reemite hacia ShellWindow con la tabla como hint
        });

        scene_->addItem(n);
        const int count = nodes_.size();
        n->setPos(40 + (count % 3) * 300, 40 + (count / 3) * 220);
        nodes_.insert(table, n);


        scene_->setSceneRect(scene_->itemsBoundingRect().marginsAdded(QMarginsF(60,60,60,60)));
    }



    void redrawAll(bool clearEdgesOnly=false) {
        // Eliminar aristas anteriores
        for (RelationEdge* e : edges_) {
            if (!e) continue;
            scene_->removeItem(static_cast<QGraphicsItem*>(e));
            delete e;
        }
        edges_.clear();

        if (!clearEdgesOnly && nodes_.isEmpty()) {
            // canvas en blanco, no auto-poblar nodos
            return;
        }

        auto& dm = DataModel::instance();
        auto idxByName = [](const Schema& s, const QString& col){
            for (int i=0;i<s.size();++i) if (s[i].name == col) return i;
            return -1;
        };

        for (const auto& child : dm.tables()) {
            for (const auto& fk : dm.relationshipsFor(child)) {
                TableNode* cnode = nodes_.value(fk.childTable, nullptr);
                TableNode* pnode = nodes_.value(fk.parentTable, nullptr);
                if (!cnode || !pnode) continue;

                const Schema sc = dm.schema(fk.childTable);
                const Schema sp = dm.schema(fk.parentTable);

                int cf = fk.childCol;
                int pf = fk.parentCol;

                // ÍNDICES INVÁLIDOS → omitir relación
                if (cf < 0 || pf < 0 || cf >= sc.size() || pf >= sp.size())
                    continue;

                auto actionToText = [](FkAction a){
                    switch(a){
                    case FkAction::Restrict: return "Restrict";
                    case FkAction::Cascade:  return "Cascade";
                    case FkAction::SetNull:  return "SetNull";
                    }
                    return "Restrict";
                };

                const QString tip = QString("%1.%2 → %3.%4\nON DELETE %5 | ON UPDATE %6")
                                        .arg(fk.childTable, sc[cf].name,
                                             fk.parentTable, sp[pf].name,
                                             actionToText(fk.onDelete),
                                             actionToText(fk.onUpdate));

                // ... dentro de redrawAll, donde hoy haces: auto* e = new RelationEdge(...tip);
                auto isUnique = [&](const QString& table, int col)->bool {
                    const Schema s = DataModel::instance().schema(table);
                    if (col < 0 || col >= s.size()) return false;
                    const auto& f = s[col];
                    if (f.pk) return true;
                    return f.indexado.contains("sin duplicados", Qt::CaseInsensitive);
                };

                const bool parentUnique = isUnique(fk.parentTable, pf);
                const bool childUnique  = isUnique(fk.childTable, cf);
                QString relType = "N:M";
                if (parentUnique && !childUnique) relType = "1:N";
                else if (parentUnique && childUnique) relType = "1:1";

                auto* e = new RelationEdge(cnode, cf, pnode, pf, tip, relType, fk.onDelete, fk.onUpdate);
                scene_->addItem(e);
                edges_.push_back(e);

            }
        }
        scene_->setSceneRect(scene_->itemsBoundingRect().marginsAdded(QMarginsF(60,60,60,60)));
    }
};


void RelationsPage::refreshSidebarFromModel() {
    refreshSidebar();  // reutiliza tu método privado
    // opcional: ajustar el rect si ya hay nodos en el canvas
    scene_->setSceneRect(scene_->itemsBoundingRect().marginsAdded(QMarginsF(60,60,60,60)));
}

ShellWindow::ShellWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("MiniAccess — Shell");
    resize(kWinW, kWinH);

    QString err;
    DataModel::instance().loadFromJson("miniaccess.json", &err);

    // =============== Barra superior con iconos ===============
    auto *topBar = new QWidget;
    topBar->setFixedHeight(kTopH);
    topBar->setStyleSheet("background-color:#9f3639;");

    auto *hl = new QHBoxLayout(topBar);
    hl->setContentsMargins(25,0,12,0);
    hl->setSpacing(27);

    auto makeBtn = [](const QString& path){
        auto *btn = new QPushButton;
        btn->setFixedSize(22,22);
        btn->setIcon(QIcon(path));
        btn->setIconSize(QSize(22,22));
        btn->setFlat(true);
        btn->setStyleSheet("background:transparent; border:none;");
        return btn;
    };

    hl->addWidget(makeBtn(":/shell/saveBtn.png"));
    hl->addWidget(makeBtn(":/shell/undoBtn.png"));
    hl->addWidget(makeBtn(":/shell/redoBtn.png"));
    hl->addStretch();

    // =============== Segunda barra con pestañas ===============
    auto *tabBar = new QWidget;
    tabBar->setFixedHeight(kTabsH);
    tabBar->setStyleSheet("background-color:#9f3639;");

    auto *tabsLayout = new QHBoxLayout(tabBar);
    tabsLayout->setContentsMargins(25,0,0,0);
    tabsLayout->setSpacing(40);

    homeBtn    = makeTabBtn("Home");
    createBtn  = makeTabBtn("Create");
    dbToolsBtn = makeTabBtn("Database Tools");

    tabsLayout->addWidget(homeBtn);
    tabsLayout->addWidget(createBtn);
    tabsLayout->addWidget(dbToolsBtn);
    tabsLayout->addStretch();

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(homeBtn, 0);
    group->addButton(createBtn, 1);
    group->addButton(dbToolsBtn, 2);
    homeBtn->setChecked(true);

    // =============== Tercera barra (contenedor 100px) ===============
    iconBar = new QWidget;
    iconBar->setFixedHeight(kRibbonH);
    iconBar->setStyleSheet("background-color:#d9d9d9; border-bottom:1px solid #b0b0b0;");

    auto *iconBarLay = new QVBoxLayout(iconBar);
    iconBarLay->setContentsMargins(8,4,8,6);
    iconBarLay->setSpacing(0);

    ribbonStack = new QStackedWidget;
    ribbonStack->setContentsMargins(0,0,0,0);

    ribbonStack->addWidget(buildHomeRibbon());
    ribbonStack->addWidget(buildCreateRibbon());
    ribbonStack->addWidget(buildDBToolsRibbon());

    iconBarLay->addWidget(ribbonStack);

    connect(homeBtn,    &QToolButton::toggled, this, [this](bool on){ if(on) ribbonStack->setCurrentIndex(0); });
    connect(createBtn,  &QToolButton::toggled, this, [this](bool on){ if(on) ribbonStack->setCurrentIndex(1); });
    connect(dbToolsBtn, &QToolButton::toggled, this, [this](bool on){ if(on) ribbonStack->setCurrentIndex(2); });

    // === PÁGINAS REALES ===
    auto *tablesPage  = new TablesPage(nullptr, false);
    auto *recordsPage = new RecordsPage;          // ← página de Registros (CRUD real)
    auto *list        = tablesPage->tableListWidget();

    // =============== Panel izquierdo (260x610) ===============
    auto *leftPanel = new QWidget;
    leftPanel->setFixedSize(kLeftW, kContentH);
    leftPanel->setStyleSheet("background:white; border-right:1px solid #b0b0b0;");

    auto *v = new QVBoxLayout(leftPanel);
    v->setContentsMargins(0,0,0,0);
    v->setSpacing(6);

    auto *hdr = new QLabel("Tablas");
    hdr->setFixedHeight(32);
    hdr->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    hdr->setStyleSheet("background:#9f3639; color:white; font-weight:bold; font-size:20px; padding:6px 8px;");

    auto *search = new QLineEdit;
    search->setFocusPolicy(Qt::ClickFocus);
    search->setPlaceholderText("Search");
    search->setFixedHeight(26);
    search->setClearButtonEnabled(true);
    search->setStyleSheet("QLineEdit{border:1px solid #c8c8c8; padding:2px 6px; margin:0 6px;}");
    search->addAction(qApp->style()->standardIcon(QStyle::SP_FileDialogContentsView), QLineEdit::LeadingPosition);

    // Lista real de TablesPage, con mismo estilo
    list->setFrameShape(QFrame::NoFrame);
    list->setIconSize(QSize(18,18));
    list->setStyleSheet(
        "QListWidget{border:none;}"
        "QListWidget::item{padding:6px 8px;}"
        "QListWidget::item:selected{background:#f4c9cc;}"
        );

    // Filtrar la lista (simple)
    QObject::connect(search, &QLineEdit::textChanged, list, [list](const QString& t){
        for (int i = 0; i < list->count(); ++i) {
            auto *it = list->item(i);
            it->setHidden(!it->text().contains(t, Qt::CaseInsensitive));
        }
    });

    v->addWidget(hdr);
    v->addWidget(search);
    v->addWidget(list, 1);

    // después de v->addWidget(list, 1);
    auto *hdrQ = new QLabel("Queries");
    hdrQ->setFixedHeight(24);
    hdrQ->setStyleSheet("background:#eee; color:#333; font-weight:bold; padding:4px 8px;");
    auto *listQueries = new QListWidget;
    listQueries->setFrameShape(QFrame::NoFrame);
    listQueries->setStyleSheet("QListWidget{border:none;} QListWidget::item{padding:6px 8px;}");

    auto refreshQueries = [listQueries](){
        listQueries->clear();
        for (auto& n : DataModel::instance().queries()) listQueries->addItem(n);
    };
    refreshQueries();
    QObject::connect(&DataModel::instance(), &DataModel::queriesChanged, this, refreshQueries);

    v->addWidget(hdrQ);
    v->addWidget(listQueries, 1);

    // abrir una consulta al hacer doble‐clic
    QObject::connect(listQueries, &QListWidget::itemDoubleClicked, this, [=](QListWidgetItem* it){
        if (!it) return;
        auto *stack = findChild<QStackedWidget*>("contentStack");
        QueryPage* qp=nullptr;
        for (int i=0;i<stack->count();++i) if ((qp=qobject_cast<QueryPage*>(stack->widget(i)))) break;
        if (!qp) return;
        // fuerza cargar en QueryPage:
        // (si seguiste el snippet del combo en QueryPage, basta con setear el texto y ejecutar)
        stack->setCurrentWidget(qp);
        // llamar a un método helper en QueryPage (crea uno si quieres) para cargar por nombre
        QMetaObject::invokeMethod(qp, "loadSavedByName", Qt::DirectConnection, Q_ARG(QString, it->text()));
    });



    // =============== Derecha: stack + reserva ===============
    auto *rightContainer = new QWidget;
    rightContainer->setFixedSize(kRightW, kContentH);
    auto *rightV = new QVBoxLayout(rightContainer);
    rightV->setContentsMargins(0,0,0,0);
    rightV->setSpacing(0);

    auto *stack = new QStackedWidget;
    stack->setFixedSize(kRightW, kStackH);
    stack->setStyleSheet("background:white; border:1px solid #b0b0b0;");
    stack->setObjectName("contentStack");


    auto makePage = [](const QString& text){
        auto *lbl = new QLabel(text);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("font-size:20px; font-weight:bold; color:#555;");
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0,0,0,0);
        layout->setSpacing(0);
        layout->addStretch();
        layout->addWidget(lbl);
        layout->addStretch();
        return page;
    };


    // Páginas en el stack (con Form)
    stack->addWidget(tablesPage);   // index 0 (Design)
    stack->addWidget(recordsPage);  // index 1 (Datasheet)

    // === NUEVO: FormPage (captura 1x1)
    auto *formPage = new FormPage;
    const int formIdx = stack->addWidget(formPage);

    // Página real de consultas
    auto *queriesPage = new QueryPage;
    const int queriesIdx = stack->addWidget(queriesPage);

    // Relaciones
    auto *relationsPage = new RelationsPage;
    stack->addWidget(relationsPage);

    // ===== NUEVO: ReportsPage =====
    auto *reportsPage = new ReportsPage;
    const int reportsIdx = stack->addWidget(reportsPage);



    connect(relationsPage, &RelationsPage::requestAddRelation, this,
            [this, relationsPage](const QString& tableHint){
                auto& dm = DataModel::instance();

                struct FK { QString ct; int cc; QString pt; int pc; FkAction del; FkAction upd; };
                auto snapshot = [&](){
                    QList<FK> out;
                    for (const auto& t : dm.tables()) {
                        for (const auto& fk : dm.relationshipsFor(t)) {
                            out.push_back({fk.childTable, fk.childCol, fk.parentTable, fk.parentCol, fk.onDelete, fk.onUpdate});
                        }
                    }
                    return out;
                };

                const auto before = snapshot();

                if (!showAddRelationDialog(this, tableHint))
                    return;

                const auto after = snapshot();
                auto same = [](const FK& a, const FK& b){
                    return a.ct==b.ct && a.cc==b.cc && a.pt==b.pt && a.pc==b.pc;
                };

                // Dibuja solo las nuevas
                for (const auto& nf : after) {
                    bool existed = false;
                    for (const auto& bf : before) if (same(nf, bf)) { existed = true; break; }
                    if (!existed) {
                        // asegúrate de que existan los nodos
                        relationsPage->ensureNodeVisible(nf.ct);
                        relationsPage->ensureNodeVisible(nf.pt);
                        // dibuja la arista
                        relationsPage->addRelationEdge(nf.ct, nf.cc, nf.pt, nf.pc, nf.del, nf.upd);
                    }
                }
            });

    // (Eliminado el antiguo: stack->addWidget(queriesPage) como "mock")

    auto *navigator = buildRecordNavigator(kRightW, kBottomReserveH, recordsPage);
    rightV->addWidget(stack);
    rightV->addWidget(navigator);

    // Mostrar navigator SOLO en Registros
    navigator->setVisible(false);
    QObject::connect(stack, &QStackedWidget::currentChanged, this, [=](int ix){
        navigator->setVisible(stack->widget(ix) == recordsPage);
    });

    // Layout horizontal
    auto *hbox = new QHBoxLayout;
    hbox->setContentsMargins(0,0,0,0);
    hbox->setSpacing(0);
    hbox->addWidget(leftPanel);
    hbox->addWidget(rightContainer);

    // Layout central
    auto *central = new QWidget(this);
    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0,0,0,0);
    vbox->setSpacing(0);
    vbox->addWidget(topBar);
    vbox->addWidget(tabBar);
    vbox->addWidget(iconBar);
    vbox->addLayout(hbox);

    setCentralWidget(central);
    ribbonStack->setCurrentIndex(0);

    // ====== Conexiones TablesPage → RecordsPage ======
    connect(tablesPage, &TablesPage::tableSelected, this, [=](const QString& name){
        // Sincroniza RecordsPage, pero NO cambiamos de vista
        recordsPage->setTableFromFieldDefs(name, tablesPage->schemaFor(name));
        // stack->setCurrentWidget(tablesPage); // mantener Design
    });

    connect(tablesPage, &TablesPage::schemaChanged, this,
            [=](const QString& name, const Schema& s){
                auto *it = list->currentItem();
                if (it && it->text() == name) {
                    recordsPage->setTableFromFieldDefs(name, s);
                }
            });

    // Click en una tabla: solo seleccionar/sincronizar, SIN navegar a Datasheet
    connect(list, &QListWidget::itemClicked, this, [=](QListWidgetItem* it){
        if (!it) return;
        const QString t = it->text();
        recordsPage->setTableFromFieldDefs(t, tablesPage->schemaFor(t));
        // NO stack->setCurrentWidget(recordsPage);
    });


    // ====== Ribbon: Views (Datasheet / Design / Form) ======
    QToolButton *btnDatasheet = nullptr, *btnDesign = nullptr, *btnForm = nullptr;
    for (auto *b : ribbonStack->widget(0)->findChildren<QToolButton*>()) {
        if (b->text() == "Datasheet") btnDatasheet = b;
        if (b->text() == "Design")    btnDesign    = b;
        if (b->text() == "Form")      btnForm      = b;
    }

    if (btnDatasheet) connect(btnDatasheet, &QToolButton::clicked, this, [=]{
            auto *it = list->currentItem();
            if (!it) return;

            const QString t = it->text();
            const Schema s  = DataModel::instance().schema(t);

            bool hasPk = false;
            for (const auto& f : s) { if (f.pk) { hasPk = true; break; } }

            if (!hasPk) {
                QMessageBox::warning(this,
                                     tr("Clave primaria requerida"),
                                     tr("Seleccione una llave primaria (PK) para continuar al Datasheet."));
                return;
            }

            recordsPage->setTableFromFieldDefs(t, s);
            stack->setCurrentWidget(recordsPage);
        });

    if (btnDesign) connect(btnDesign, &QToolButton::clicked, this, [=]{
            // Bloqueo: no salir del Datasheet si hay requeridos vacíos
            if (stack->currentWidget() == recordsPage) {
                QModelIndex idx;
                if (recordsPage->hasUnfilledRequired(&idx)) {
                    auto *sheet = recordsPage->sheet(); // o findChild<QTableWidget*>("twRegistros")
                    sheet->setCurrentCell(idx.row(), idx.column());
                    QToolTip::showText(
                        sheet->viewport()->mapToGlobal(
                            sheet->visualItemRect(sheet->item(idx.row(), idx.column())).bottomRight()),
                        tr("Complete el campo requerido para continuar."),
                        sheet
                        );
                    return; // ← NO cambiamos a Design
                }
            }
            // OK: cambiar a Design
            stack->setCurrentWidget(tablesPage);
        });

    // === NUEVO: botón "Form" → abre FormPage y sincroniza con la tabla seleccionada
    if (btnForm) connect(btnForm, &QToolButton::clicked, this, [=]{
            if (auto *it = list->currentItem()) {
                const QString t = it->text();
                formPage->setTable(t, tablesPage->schemaFor(t));
            }
            stack->setCurrentIndex(formIdx);
        });


    if (btnDesign) connect(btnDesign, &QToolButton::clicked, this, [=]{
            // Bloqueo: no salir del Datasheet si hay requeridos vacíos
            if (stack->currentWidget() == recordsPage) {
                QModelIndex idx;
                if (recordsPage->hasUnfilledRequired(&idx)) {
                    auto *sheet = recordsPage->sheet(); // o findChild<QTableWidget*>("twRegistros")
                    sheet->setCurrentCell(idx.row(), idx.column());
                    QToolTip::showText(
                        sheet->viewport()->mapToGlobal(
                            sheet->visualItemRect(sheet->item(idx.row(), idx.column())).bottomRight()),
                        tr("Complete el campo requerido para continuar."),
                        sheet
                        );
                    return; // ← NO cambiamos a Design
                }
            }

            // OK: cambiar a Design
            stack->setCurrentWidget(tablesPage);
        });


    // Deshabilitar el botón de la vista actual y habilitar el otro
    auto updateViewsUi = [=]{
        const bool inDatasheet = (stack->currentWidget() == recordsPage);
        const bool inDesign    = (stack->currentWidget() == tablesPage);
        if (btnDatasheet) btnDatasheet->setEnabled(!inDatasheet);
        if (btnDesign)    btnDesign->setEnabled(!inDesign);
    };
    // Inicializa el estado actual
    updateViewsUi();
    // Mantenerlo sincronizado al cambiar de página
    QObject::connect(stack, &QStackedWidget::currentChanged, this, [=](int){
        updateViewsUi();
    });

    // ====== Ribbon: Records / Find (wire actions) ======
    if (auto *homeRibbon = ribbonStack->widget(0)) {
        const auto btns = homeRibbon->findChildren<QToolButton*>();
        for (auto *b : btns) {
            const QString t = b->text();
            if (t.compare("New", Qt::CaseInsensitive) == 0) {
                connect(b, &QToolButton::clicked, recordsPage, [recordsPage]{
                    QMetaObject::invokeMethod(recordsPage, "onInsertar", Qt::DirectConnection);
                });
            } else if (t.compare("Delete", Qt::CaseInsensitive) == 0) {
                connect(b, &QToolButton::clicked, recordsPage, [recordsPage]{
                    QMetaObject::invokeMethod(recordsPage, "onEliminar", Qt::DirectConnection);
                });
            } else if (t.compare("Find", Qt::CaseInsensitive) == 0) {
                connect(b, &QToolButton::clicked, recordsPage, [recordsPage]{
                    QMetaObject::invokeMethod(recordsPage, "onLimpiarBusqueda", Qt::DirectConnection);
                });
            } else if (t.compare("Sort Asc", Qt::CaseInsensitive) == 0) {
                connect(b, &QToolButton::clicked, recordsPage, &RecordsPage::sortAscending);
            } else if (t.compare("Sort Desc", Qt::CaseInsensitive) == 0) {
                connect(b, &QToolButton::clicked, recordsPage, &RecordsPage::sortDescending);
            } else if (t.compare("Clear", Qt::CaseInsensitive) == 0) {
                connect(b, &QToolButton::clicked, recordsPage, &RecordsPage::clearSorting);
            } else if (t.compare("Filter", Qt::CaseInsensitive) == 0) {
                connect(b, &QToolButton::clicked, recordsPage, &RecordsPage::showFilterMenu);
            } else if (t.compare("Reportes", Qt::CaseInsensitive) == 0) {
                // <<< NUEVO: abrir la página de Reportes >>>
                connect(b, &QToolButton::clicked, this, [stack, reportsIdx]{
                    stack->setCurrentIndex(reportsIdx);
                });
            }
            // "Save" se deja como placeholder (no aplica en el flujo actual)
        }
    }


    // ====== Create → Queries ======
    // 1) Dock con el diseñador visual (clase aparte, sin .ui)
    QDockWidget* accessDock = new QDockWidget("Consultas (Visual)", this);
    accessDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* accessDesigner = new AccessQueryDesignerPage;
    accessDock->setWidget(accessDesigner);
    addDockWidget(Qt::LeftDockWidgetArea, accessDock);
    accessDock->hide(); // se muestra al pulsar "Query Design"

    // 2) Conectar ejecuciones del diseñador a QueryPage
    connect(accessDesigner, &AccessQueryDesignerPage::runSql, this,
            [stack, queriesPage, queriesIdx](const QString& sql){
                // mostrar la pestaña de resultados
                stack->setCurrentIndex(queriesIdx);
                QMetaObject::invokeMethod(queriesPage, "setSqlText", Qt::DirectConnection,
                                          Q_ARG(QString, sql));
                QMetaObject::invokeMethod(queriesPage, "runQuery", Qt::DirectConnection);
            });

    // 3) Cuando se guarda/renombra/elimina una consulta → refrescar lista izquierda
    connect(accessDesigner, &AccessQueryDesignerPage::savedQuery, this,
            [refreshQueries](const QString&){ refreshQueries(); });

    // 4) Botones del ribbon Create:
    if (auto *createRibbonW = ribbonStack->widget(1)) {
        for (auto *b : createRibbonW->findChildren<QToolButton*>()) {
            const QString t = b->text();
            if (t == "Query Wizard") {
                // Abrimos la página de resultados/SQL crudo
                connect(b, &QToolButton::clicked, this, [stack, queriesIdx]{
                    stack->setCurrentIndex(queriesIdx);
                });
            } else if (t == "Query Design") {
                // Abrimos el diseñador visual estilo Access (dock)
                connect(b, &QToolButton::clicked, this, [accessDock]{
                    accessDock->setVisible(true);
                    accessDock->raise();
                });
            }
        }
    }

    // ====== Enlazar estado de navegación (RecordsPage → barra inferior) ======
    auto posLbl   = navigator->findChild<QLabel*>("lblPos");
    auto firstBtn = navigator->findChild<QToolButton*>("btnFirst");
    auto prevBtn  = navigator->findChild<QToolButton*>("btnPrev");
    auto nextBtn  = navigator->findChild<QToolButton*>("btnNext");
    auto lastBtn  = navigator->findChild<QToolButton*>("btnLast");

    QObject::connect(recordsPage, &RecordsPage::navState, this,
                     [posLbl, firstBtn, prevBtn, nextBtn, lastBtn](int cur, int tot, bool canPrev, bool canNext){
                         if (posLbl)  posLbl->setText(QString("%1 of %2").arg(tot == 0 ? 0 : cur).arg(tot));
                         if (firstBtn) firstBtn->setEnabled(canPrev);
                         if (prevBtn)  prevBtn->setEnabled(canPrev);
                         if (nextBtn)  nextBtn->setEnabled(canNext);
                         if (lastBtn)  lastBtn->setEnabled(canNext);
                     });

    // Estado inicial seguro y disparo de actualización
    if (posLbl)  posLbl->setText("0 of 0");
    if (firstBtn) firstBtn->setEnabled(false);
    if (prevBtn)  prevBtn->setEnabled(false);
    if (nextBtn)  nextBtn->setEnabled(false);
    if (lastBtn)  lastBtn->setEnabled(false);
    QMetaObject::invokeMethod(recordsPage, "onSelectionChanged", Qt::QueuedConnection);
}

// Centrar ventana
void ShellWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    if (auto *scr = screen()) {
        const QRect avail = scr->availableGeometry();
        move(avail.center() - frameGeometry().center());
    }
}

void ShellWindow::closeEvent(QCloseEvent* e) {
    QString err;
    DataModel::instance().saveToJson("miniaccess.json", &err);
    QMainWindow::closeEvent(e);
}

// ------------------ PÁGINAS DEL RIBBON ------------------

QWidget* ShellWindow::buildHomeRibbon() {
    auto *wrap = new QWidget;
    auto *hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(8,2,8,2);
    hl->setSpacing(16);

    // Quitamos "Navigation" del Ribbon (la navegación ya está en la barra inferior)

    auto *gSort = makeRibbonGroup("Sort & Filter", {
                                                       makeActionBtn("Sort Asc"),
                                                       makeActionBtn("Sort Desc"),
                                                       makeActionBtn("Filter"),
                                                       makeActionBtn("Clear")
                                                   });

    auto *gViews = makeRibbonGroup("Views", {
                                                makeActionBtn("Datasheet"),
                                                makeActionBtn("Design"),
                                                makeActionBtn("Form")   // << NUEVO
                                            });


    auto *gReportes = makeRibbonGroup("Reportes", {
        makeActionBtn("Reportes")
    });


    hl->addWidget(gSort);
    hl->addWidget(vSep());
    hl->addWidget(gViews);
    hl->addWidget(vSep());
    hl->addWidget(gReportes);
    hl->addStretch();
    return wrap;

}

QWidget* ShellWindow::buildCreateRibbon() {
    auto *wrap = new QWidget;
    auto *hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(8,2,8,2);
    hl->setSpacing(16);

    auto *gTable = makeRibbonGroup("Tables", {
        makeActionBtn("Table")
    });


    auto *gQuery = makeRibbonGroup("Queries", {
                                                  makeActionBtn("Query Wizard"),
                                                  makeActionBtn("Query Design")
                                              });

    auto *gFormularios = makeRibbonGroup("Formularios", {
        makeActionBtn("Formularios")
    });


    hl->addWidget(gTable);
    hl->addWidget(vSep());
    hl->addWidget(gQuery);
    hl->addWidget(vSep());
    hl->addWidget(gFormularios);
    hl->addStretch();

    // --- Placeholders para botones de Tables (dejamos Queries sin popup; se conectan en el ctor) ---
    const auto btnsCreate = wrap->findChildren<QToolButton*>();
    for (auto *b : btnsCreate) {
        const QString t = b->text();

        if (t == "Table") {
            connect(b, &QToolButton::clicked, this, [this]{
                bool ok=false;
                const QString name = QInputDialog::getText(this, tr("Nueva tabla"),
                                                           tr("Nombre de la nueva tabla:"),
                                                           QLineEdit::Normal, "NuevaTabla", &ok);
                if (!ok || name.trimmed().isEmpty()) return;
                QString err;
                Schema s;
                {
                    FieldDef f;
                    f.name = "ID";
                    f.type = "Autonumeración";
                    f.pk   = true;
                    f.autoSubtipo  = "Long Integer";
                    f.autoNewValues = "Increment";
                    f.formato = "General Number";

                    f.requerido = true;
                    f.indexado  = "Sí (sin duplicados)";
                    s.append(f);
                }

                if (!DataModel::instance().createTable(name.trimmed(), s, &err)) {
                    QMessageBox::warning(this, tr("Crear tabla"), err);
                    return;
                }

/* === Forzar volver a Home (selección visual y ribbon) === */

// 1) Marca el botón Home y desmarca Create (según lo que tengas disponible)
#ifdef HAS_HOMEBTN_MEMBER
                homeBtn->setChecked(true);                 // si tienes puntero miembro
                createBtn->setChecked(false);
#else
                // Fallback: búscalos por texto
                for (auto *tb : this->findChildren<QToolButton*>()) {
                    if (tb->text() == "Home")   tb->setChecked(true);
                    if (tb->text() == "Create") tb->setChecked(false);
                }
#endif


                // 2) Muestra el ribbon de Home (página 0)
                auto ribbons = this->findChildren<QStackedWidget*>();
                if (!ribbons.isEmpty()) ribbons.first()->setCurrentIndex(0);

            });


        } else if (t == "Table Design") {
            connect(b, &QToolButton::clicked, this, [this, t]{
                QMessageBox::information(this, "Create",
                                         QString("Placeholder: %1 (abriría diseñador de tablas).").arg(t));
            });
        }
        // NOTA: "Query Wizard"/"Query Design" se conectan en el constructor para abrir QueryPage o el dock visual
    }
    return wrap;
}


QWidget* ShellWindow::buildDBToolsRibbon() {
    auto *wrap = new QWidget;
    auto *hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(8,2,8,2);
    hl->setSpacing(16);

    auto *gRel     = makeRibbonGroup("Relationships", { makeActionBtn("Relationships") });

    hl->addWidget(gRel);
    hl->addWidget(vSep());
    hl->addStretch();

    // --- Actions para "Database Tools"
    const auto btnsDb = wrap->findChildren<QToolButton*>();
    for (auto *b : btnsDb) {
        const QString t = b->text();

        if (t == "Indexes") {
            connect(b, &QToolButton::clicked, this, [this]{
                QMessageBox::information(this, "Database Tools",
                                         "Placeholder: UI de índices (mock).");
            });

        } else if (t == "Relationships") {
            connect(b, &QToolButton::clicked, this, [this]{
                auto *content = this->findChild<QStackedWidget*>("contentStack");
                if (!content) return;

                RelationsPage* relPage = nullptr;
                for (int i = 0; i < content->count(); ++i) {
                    if (auto rp = qobject_cast<RelationsPage*>(content->widget(i))) {
                        relPage = rp; break;
                    }
                }
                if (!relPage) return;

                relPage->refreshSidebarFromModel();
                relPage->startBlank();
                content->setCurrentWidget(relPage);
            });

        } else if (t == "Avail List") {
            // <<< CAMBIO: abrir Records (Datasheet) con la tabla seleccionada/primera >>>
            connect(b, &QToolButton::clicked, this, [this]{
                // 1) Localizar el stack principal
                auto *content = this->findChild<QStackedWidget*>("contentStack");
                if (!content) return;

                // 2) Encontrar RecordsPage y TablesPage dentro del stack
                RecordsPage* recPage = nullptr;
                TablesPage*  tabPage = nullptr;
                for (int i = 0; i < content->count(); ++i) {
                    if (!recPage) recPage = qobject_cast<RecordsPage*>(content->widget(i));
                    if (!tabPage) tabPage = qobject_cast<TablesPage*>(content->widget(i));
                }
                if (!recPage || !tabPage) return;

                // 3) Tomar la tabla seleccionada en el panel izquierdo (o la primera disponible)
                QString tableName;
                if (auto *list = tabPage->tableListWidget()) {
                    if (auto *it = list->currentItem()) tableName = it->text();
                }
                if (tableName.isEmpty()) {
                    const auto tables = DataModel::instance().tables();
                    if (!tables.isEmpty()) tableName = tables.first();
                }

                // 4) Sincronizar RecordsPage con esa tabla y mostrar la vista Datasheet
                if (!tableName.isEmpty()) {
                    recPage->setTableFromFieldDefs(tableName, tabPage->schemaFor(tableName));
                }
                content->setCurrentWidget(recPage);
            });

        } else if (t == "Compact") {
            connect(b, &QToolButton::clicked, this, [this]{
                QMessageBox::information(this, "Database Tools",
                                         "Placeholder: Compact (mock).");
            });
        }
    }

    return wrap;
}

// ======== Stubs de compatibilidad (evitan "undefined reference") ========
void ShellWindow::refreshSavedQueries() {}
void ShellWindow::onQueryActivated(QListWidgetItem*) {}
void ShellWindow::onShowContextMenu(const QPoint&) {}
void ShellWindow::onNewQuery() {}
void ShellWindow::onRenameSelectedQuery() {}
void ShellWindow::onDeleteSelectedQuery() {}
void ShellWindow::onOpenInDesigner() {}
void ShellWindow::onRunSelectedQuery() {}
void ShellWindow::onDesignerSaved(const QString&) {}
// ========================================================================

#include "shellwindow.moc"
