#include "shellwindow.h"
#include "tablespage.h"
#include "recordspage.h"
#include "datamodel.h"

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
    search->setObjectName("navSearch");
    search->setPlaceholderText("Search");
    search->setFixedWidth(240);
    search->setStyleSheet("background:white; border:1px solid #c8c8c8; padding:2px 6px;");

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
            icon = qApp->style()->standardIcon(QStyle::SP_BrowserStop);
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

// ------------------------------------------------------

ShellWindow::ShellWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("MiniAccess — Shell");
    resize(kWinW, kWinH);

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

    // =============== Derecha: stack + reserva ===============
    auto *rightContainer = new QWidget;
    rightContainer->setFixedSize(kRightW, kContentH);
    auto *rightV = new QVBoxLayout(rightContainer);
    rightV->setContentsMargins(0,0,0,0);
    rightV->setSpacing(0);

    auto *stack = new QStackedWidget;
    stack->setFixedSize(kRightW, kStackH);
    stack->setStyleSheet("background:white; border:1px solid #b0b0b0;");

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

    stack->addWidget(tablesPage);            // index 0 (Design)
    stack->addWidget(recordsPage);           // index 1 (Datasheet)
    stack->addWidget(makePage("Consultas"));
    stack->addWidget(makePage("Relaciones"));

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
    connect(tablesPage, &TablesPage::tableSelected, this,
            [=](const QString& name){
                recordsPage->setTableFromFieldDefs(name, tablesPage->schemaFor(name));
                stack->setCurrentWidget(recordsPage); // ir a Datasheet
            });

    connect(tablesPage, &TablesPage::schemaChanged, this,
            [=](const QString& name, const Schema& s){
                auto *it = list->currentItem();
                if (it && it->text() == name) {
                    recordsPage->setTableFromFieldDefs(name, s);
                }
            });

    // También reaccionar a clicks directos en la lista
    connect(list, &QListWidget::itemClicked, this, [=](QListWidgetItem* it){
        if (!it) return;
        const QString t = it->text();
        recordsPage->setTableFromFieldDefs(t, tablesPage->schemaFor(t));
        stack->setCurrentWidget(recordsPage);
    });

    // ====== Ribbon: Views (Datasheet / Design) ======
    QToolButton *btnDatasheet = nullptr, *btnDesign = nullptr;
    for (auto *b : ribbonStack->widget(0)->findChildren<QToolButton*>()) {
        if (b->text() == "Datasheet") btnDatasheet = b;
        if (b->text() == "Design")    btnDesign    = b;
    }
    if (btnDatasheet) connect(btnDatasheet, &QToolButton::clicked, this, [=]{
            auto *it = list->currentItem();
            if (it) {
                const QString t = it->text();
                recordsPage->setTableFromFieldDefs(t, tablesPage->schemaFor(t));
            }
            stack->setCurrentWidget(recordsPage);
        });
    if (btnDesign) connect(btnDesign, &QToolButton::clicked, this, [=]{
            stack->setCurrentWidget(tablesPage);
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
            }
            // "Save" se deja como placeholder (no aplica en el flujo actual)
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

// ------------------ PÁGINAS DEL RIBBON ------------------

QWidget* ShellWindow::buildHomeRibbon() {
    auto *wrap = new QWidget;
    auto *hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(8,2,8,2);
    hl->setSpacing(16);

    auto *gRecords = makeRibbonGroup("Records", {
                                                    makeActionBtn("New"),
                                                    makeActionBtn("Save"),
                                                    makeActionBtn("Delete")
                                                });

    // Quitamos "Navigation" del Ribbon (la navegación ya está en la barra inferior)

    auto *gSort = makeRibbonGroup("Sort & Filter", {
                                                       makeActionBtn("Sort Asc"),
                                                       makeActionBtn("Sort Desc"),
                                                       makeActionBtn("Filter"),
                                                       makeActionBtn("Clear")
                                                   });

    auto *gViews = makeRibbonGroup("Views", {
                                                makeActionBtn("Datasheet"),
                                                makeActionBtn("Design")
                                            });

    auto *gFind = makeRibbonGroup("Find", {
        makeActionBtn("Find")
    });

    hl->addWidget(gRecords);
    hl->addWidget(vSep());
    hl->addWidget(gSort);
    hl->addWidget(vSep());
    hl->addWidget(gViews);
    hl->addWidget(vSep());
    hl->addWidget(gFind);
    hl->addStretch();
    return wrap;
}

QWidget* ShellWindow::buildCreateRibbon() {
    auto *wrap = new QWidget;
    auto *hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(8,2,8,2);
    hl->setSpacing(16);

    auto *gTable = makeRibbonGroup("Tables", {
                                                 makeActionBtn("Table"),
                                                 makeActionBtn("Table Design")
                                             });

    auto *gQuery = makeRibbonGroup("Queries", {
                                                  makeActionBtn("Query Wizard"),
                                                  makeActionBtn("Query Design")
                                              });

    hl->addWidget(gTable);
    hl->addWidget(vSep());
    hl->addWidget(gQuery);
    hl->addStretch();
    return wrap;
}

QWidget* ShellWindow::buildDBToolsRibbon() {
    auto *wrap = new QWidget;
    auto *hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(8,2,8,2);
    hl->setSpacing(16);

    auto *gIdx = makeRibbonGroup("Indexes", { makeActionBtn("Indexes") });
    auto *gRel = makeRibbonGroup("Relationships", { makeActionBtn("Relationships") });
    auto *gAvail = makeRibbonGroup("Avail List", { makeActionBtn("Avail List") });
    auto *gCompact = makeRibbonGroup("Compact", { makeActionBtn("Compact") });

    hl->addWidget(gIdx);
    hl->addWidget(vSep());
    hl->addWidget(gRel);
    hl->addWidget(vSep());
    hl->addWidget(gAvail);
    hl->addWidget(vSep());
    hl->addWidget(gCompact);
    hl->addStretch();
    return wrap;
}
