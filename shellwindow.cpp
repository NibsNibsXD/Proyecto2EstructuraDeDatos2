#include "shellwindow.h"
#include "tablespage.h"

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
    sep->setStyleSheet("color:#c8c8c8;");  // tono como Access
    sep->setFixedHeight(h);                // alto visible
    return sep;
}
// --- Panel izquierdo tipo Access ---
static QWidget* buildLeftPanel(int width, int height) {
    auto *panel = new QWidget;
    panel->setFixedSize(width, height);
    panel->setStyleSheet("background:white; border-right:1px solid #b0b0b0;");

    auto *v = new QVBoxLayout(panel);
    v->setContentsMargins(0,0,0,0);
    v->setSpacing(6); // ← DA RESPIRO ENTRE HEADER Y SEARCH

    // Encabezado "Tablas" (burdeos)
    auto *hdr = new QLabel("Tablas");
    hdr->setFixedHeight(32);
    hdr->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    hdr->setStyleSheet("background:#9f3639; color:white; font-weight:bold; font-size:20px; padding:6px 8px;");

    // Caja de búsqueda con icono de lupa
    auto *search = new QLineEdit;
    search->setPlaceholderText("Search");
    search->setFixedHeight(26);
    search->setClearButtonEnabled(true);
    search->setStyleSheet(
        "QLineEdit{border:1px solid #c8c8c8; padding:2px 6px; margin:0 6px;}"
        );
    // Lupa (leading action)
    search->addAction(
        qApp->style()->standardIcon(QStyle::SP_FileDialogContentsView),
        QLineEdit::LeadingPosition
        );


    // Lista
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

// --- NUEVO: barra de navegación tipo Access (1140x36) ---
static QWidget* buildRecordNavigator(int width, int height) {
    auto *bar = new QWidget;
    bar->setFixedSize(width, height);
    bar->setStyleSheet("background:#f3f3f3; border-top:1px solid #c8c8c8;");

    auto *hl = new QHBoxLayout(bar);
    hl->setContentsMargins(8,4,8,4);
    hl->setSpacing(10);

    // "Record:"
    auto *recLbl = new QLabel("Record:");
    recLbl->setStyleSheet("color:#444; background:transparent; border:none;");

    // Botones navegación (sin borde)
    auto makeNav = [](QStyle::StandardPixmap sp, bool enabled=true){
        auto *b = new QToolButton;
        b->setAutoRaise(false);
        b->setIcon(qApp->style()->standardIcon(sp));
        b->setEnabled(enabled);
        b->setStyleSheet("QToolButton { border:none; background:transparent; }");
        return b;
    };
    auto *firstBtn = makeNav(QStyle::SP_MediaSkipBackward, true);
    auto *prevBtn  = makeNav(QStyle::SP_ArrowBack, true);
    auto *nextBtn  = makeNav(QStyle::SP_ArrowForward, false);
    auto *lastBtn  = makeNav(QStyle::SP_MediaSkipForward, false);

    // "1 of 1"
    auto *pos = new QLabel("1 of 1");
    pos->setStyleSheet("background:transparent; border:none; color:#333; padding:2px;");

    // Botón de filtro ON/OFF
    auto *filterBtn = new QToolButton;
    filterBtn->setCheckable(true);
    filterBtn->setAutoRaise(true);
    filterBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_DialogCancelButton)); // off
    filterBtn->setStyleSheet("QToolButton { border:none; background:transparent; }");

    auto *filterLbl = new QLabel("No Filter");
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

    // Search box (ancho fijo grande)
    auto *search = new QLineEdit;
    search->setPlaceholderText("Search");
    search->setFixedWidth(240);
    search->setStyleSheet("background:white; border:1px solid #c8c8c8; padding:2px 6px;");

    // Ensamble
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
        // Elegir un ícono default según el texto
        if (text.contains("New", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_FileIcon);
        else if (text.contains("Save", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_DialogSaveButton);
        else if (text.contains("Delete", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_TrashIcon);
        else if (text.contains("First", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_MediaSkipBackward);
        else if (text.contains("Prev", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_ArrowBack);
        else if (text.contains("Next", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_ArrowForward);
        else if (text.contains("Last", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_MediaSkipForward);
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
        else if (text.contains("Index", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView);
        else if (text.contains("Relationship", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_DirLinkIcon);
        else if (text.contains("Avail", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_FileDialogListView);
        else if (text.contains("Compact", Qt::CaseInsensitive))
            icon = qApp->style()->standardIcon(QStyle::SP_DialogResetButton);


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


static QWidget* makeRibbonGroup(const QString& title, const QList<QToolButton*>& buttons) {
    auto *wrap = new QWidget;
    wrap->setAttribute(Qt::WA_StyledBackground, true);
    wrap->setStyleSheet("background:transparent; border:none;");
    auto *vl = new QVBoxLayout(wrap);
    vl->setContentsMargins(6,6,6,0);
    vl->setSpacing(0);

    // título arriba
    auto *cap = new QLabel(title);
    cap->setAlignment(Qt::AlignHCenter);
    cap->setMargin(0);
    cap->setContentsMargins(0,0,0,2);
    cap->setStyleSheet("font-size:12px; font-weight:bold; "
                       "color:#222; background:transparent; border:none; padding:0;");

    // fila de botones centrada
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0,0,0,0);
    row->setSpacing(8);
    row->addStretch();
    for (auto *btn : buttons) row->addWidget(btn);
    row->addStretch();

    // contenedor para la fila
    auto *frame = new QWidget;
    frame->setAttribute(Qt::WA_StyledBackground, true);
    frame->setStyleSheet("background:transparent; border:none;");
    auto *frameLay = new QVBoxLayout(frame);
    frameLay->setContentsMargins(8,6,8,6);
    frameLay->setSpacing(0);
    frameLay->addLayout(row);

    // ahora el orden es título arriba, fila abajo
    vl->addWidget(cap);
    vl->addWidget(frame);
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

    // === PÁGINA TABLAS REAL ===
    auto *tablesPage = new TablesPage(nullptr, false);
    auto *list = tablesPage->tableListWidget();

    // =============== Panel izquierdo tipo Access (260x610) ===============
    auto *leftPanel = new QWidget;
    leftPanel->setFixedSize(kLeftW, kContentH);
    leftPanel->setStyleSheet("background:white; border-right:1px solid #b0b0b0;");

    auto *v = new QVBoxLayout(leftPanel);
    v->setContentsMargins(0,0,0,0);
    v->setSpacing(6);

    // Encabezado "Tablas"
    auto *hdr = new QLabel("Tablas");
    hdr->setFixedHeight(32);
    hdr->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    hdr->setStyleSheet("background:#9f3639; color:white; font-weight:bold; font-size:20px; padding:6px 8px;");

    // Search con icono default Qt
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

    stack->addWidget(tablesPage);
    stack->addWidget(makePage("Registros"));
    stack->addWidget(makePage("Consultas"));
    stack->addWidget(makePage("Relaciones"));

    auto *navigator = buildRecordNavigator(kRightW, kBottomReserveH);
    rightV->addWidget(stack);
    rightV->addWidget(navigator);

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

    // Conexión selección lista
    connect(list, &QListWidget::itemClicked, this, [=](QListWidgetItem*){
        stack->setCurrentWidget(tablesPage);
    });
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

    auto *gNav = makeRibbonGroup("Navigation", {
                                                   makeActionBtn("First"),
                                                   makeActionBtn("Prev"),
                                                   makeActionBtn("Next"),
                                                   makeActionBtn("Last"),
                                                   makeActionBtn("Find")
                                               });

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

    hl->addWidget(gRecords);
    hl->addWidget(vSep());
    hl->addWidget(gNav);
    hl->addWidget(vSep());
    hl->addWidget(gSort);
    hl->addWidget(vSep());
    hl->addWidget(gViews);
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

    // Indexes (único botón)
    auto *gIdx = makeRibbonGroup("Indexes", {
                                                makeActionBtn("Indexes")   // caerá en el icono default de "Index"
                                            });

    // Relationships (nuevo grupo)
    auto *gRel = makeRibbonGroup("Relationships", {
        makeActionBtn("Relationships")
    });

    // Avail List
    auto *gAvail = makeRibbonGroup("Avail List", {
        makeActionBtn("Avail List")
    });

    // Compact
    auto *gCompact = makeRibbonGroup("Compact", {
        makeActionBtn("Compact")
    });

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

