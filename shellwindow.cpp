#include "shellwindow.h"

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

// Tamaños base
static constexpr int kWinW   = 1400;
static constexpr int kWinH   = 800;
static constexpr int kTopH   = 45;   // barra iconos
static constexpr int kTabsH  = 45;   // barra tabs
static constexpr int kRibbonH= 100;  // "iconos menus" (ajustado)

// Área visible por debajo de las barras
static constexpr int kContentH = kWinH - (kTopH + kTabsH + kRibbonH); // 610
static constexpr int kLeftW  = 260;
static constexpr int kRightW = kWinW - kLeftW;                        // 1140

// Alturas en la derecha
static constexpr int kBottomReserveH = 36;                            // <-- pedido
static constexpr int kStackH = kContentH - kBottomReserveH;          // 574

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

    auto makeTab = [](const QString& text){
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
    };

    QToolButton* homeBtn     = makeTab("Home");
    QToolButton* createBtn   = makeTab("Create");
    QToolButton* externalBtn = makeTab("External Data");
    QToolButton* dbToolsBtn  = makeTab("Database Tools");

    tabsLayout->addWidget(homeBtn);
    tabsLayout->addWidget(createBtn);
    tabsLayout->addWidget(externalBtn);
    tabsLayout->addWidget(dbToolsBtn);
    tabsLayout->addStretch();

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(homeBtn);
    group->addButton(createBtn);
    group->addButton(externalBtn);
    group->addButton(dbToolsBtn);
    homeBtn->setChecked(true);

    // =============== Tercera barra (iconos menus) ===============
    auto *iconBar = new QWidget;
    iconBar->setFixedHeight(kRibbonH);
    iconBar->setStyleSheet("background-color:#d9d9d9; border-bottom:1px solid #b0b0b0;");

    auto *iconLayout = new QHBoxLayout(iconBar);
    iconLayout->setContentsMargins(0,0,0,0);
    iconLayout->setSpacing(0);

    auto *title = new QLabel("iconos menus");
    title->setStyleSheet("font-size:18px; font-weight:bold; color:#333;");
    title->setAlignment(Qt::AlignCenter);

    iconLayout->addStretch();
    iconLayout->addWidget(title);
    iconLayout->addStretch();

    // =============== Panel izquierdo (260x610) ===============
    auto *leftPanel = new QWidget;
    leftPanel->setFixedSize(kLeftW, kContentH);
    leftPanel->setStyleSheet("background-color:#d9d9d9; border-right:1px solid #b0b0b0;");

    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0,0,0,0);
    leftLayout->setSpacing(0);

    auto *leftTitle = new QLabel("titulos tablas");
    leftTitle->setStyleSheet("font-size:16px; font-weight:bold; color:#333;");
    leftTitle->setAlignment(Qt::AlignCenter);

    leftLayout->addStretch();
    leftLayout->addWidget(leftTitle);
    leftLayout->addStretch();

    // =============== Derecha: contenedor (1140x610) con stack + reserva ===============
    auto *rightContainer = new QWidget;
    rightContainer->setFixedSize(kRightW, kContentH);
    auto *rightV = new QVBoxLayout(rightContainer);
    rightV->setContentsMargins(0,0,0,0);
    rightV->setSpacing(0);

    // Stack arriba (1140x574)
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
    stack->addWidget(makePage("Tablas"));
    stack->addWidget(makePage("Registros"));
    stack->addWidget(makePage("Consultas"));
    stack->addWidget(makePage("Relaciones"));

    // Reserva inferior fija (1140x36)
    auto *bottomReserve = new QWidget;
    bottomReserve->setFixedSize(kRightW, kBottomReserveH);
    bottomReserve->setStyleSheet("background:transparent;"); // o "#f4f4f4" si quieres verla

    rightV->addWidget(stack);
    rightV->addWidget(bottomReserve);

    // =============== Layout horizontal ===============
    auto *hbox = new QHBoxLayout;
    hbox->setContentsMargins(0,0,0,0);
    hbox->setSpacing(0);
    hbox->addWidget(leftPanel);
    hbox->addWidget(rightContainer);

    // =============== Layout central ===============
    auto *central = new QWidget(this);
    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0,0,0,0);
    vbox->setSpacing(0);
    vbox->addWidget(topBar);
    vbox->addWidget(tabBar);
    vbox->addWidget(iconBar);
    vbox->addLayout(hbox);

    setCentralWidget(central);
}

void ShellWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    if (auto *scr = screen()) {
        const QRect avail = scr->availableGeometry();
        move(avail.center() - frameGeometry().center());
    }
}
