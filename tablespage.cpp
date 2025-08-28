#include "tablespage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QInputDialog>
#include <QRegularExpression>
#include <algorithm>
#include <QApplication>

/* ===== Helpers ===== */
static QStringList kTypes() {
    return {"Autonumeración","Número","Fecha/Hora","Moneda","Texto corto"};
}

/* ===== Constructor ===== */
TablesPage::TablesPage(QWidget *parent, bool withSidebar)
    : QWidget(parent), withSidebar_(withSidebar) {
    setupUi();
    setupFakeData();
    applyQss();

    tablesList->setCurrentRow(0);
    onSelectTable();
}

/* ====================== UI ====================== */
void TablesPage::setupUi() {
    setWindowTitle("MiniAccess – Diseño de Tablas");

    QHBoxLayout *root = new QHBoxLayout(this);

    // -------- Panel izquierdo: lista de tablas --------
    if (withSidebar_) {
        QVBoxLayout *left = new QVBoxLayout;
        QLabel *lblTablas = new QLabel("Tablas");
        lblTablas->setObjectName("lblSideTitle");
        tablesList = new QListWidget;
        tablesList->setMinimumWidth(190);
        left->addWidget(lblTablas);
        left->addWidget(tablesList, 1);
        root->addLayout(left, 0);
    } else {
        tablesList = new QListWidget;  // sigue existiendo, pero no se muestra
    }

    // -------- Panel central --------
    QVBoxLayout *center = new QVBoxLayout;

    // Barra superior
    QHBoxLayout *topBar = new QHBoxLayout;
    tableNameEdit = new QLineEdit;
    tableNameEdit->setPlaceholderText("Nombre de la tabla (p. ej. Matricula)");

    // NUEVO: descripción
    tableDescEdit = new QLineEdit;
    tableDescEdit->setPlaceholderText("Descripción de la tabla (opcional)");

    btnNueva    = new QPushButton("Nueva");
    btnEditar   = new QPushButton("Renombrar");
    btnEliminar = new QPushButton("Eliminar");

    topBar->addWidget(new QLabel("Diseñando:"));
    topBar->addWidget(tableNameEdit, 1);
    topBar->addSpacing(8);
    topBar->addWidget(new QLabel("Descripción:"));
    topBar->addWidget(tableDescEdit, 1);
    topBar->addStretch();
    topBar->addWidget(btnNueva);
    topBar->addWidget(btnEditar);
    topBar->addWidget(btnEliminar);

    // Grid central
    fieldsTable = new QTableWidget(0, 4);
    fieldsTable->setHorizontalHeaderLabels(
        {"Nombre del campo", "Tipo de datos", "Tamaño", "PK"});
    fieldsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    fieldsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    fieldsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    fieldsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    fieldsTable->setAlternatingRowColors(true);
    fieldsTable->verticalHeader()->setVisible(false);
    fieldsTable->horizontalHeader()->setHighlightSections(false);
    fieldsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    fieldsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    fieldsTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);

    // Botones de campos
    QHBoxLayout *fieldBtns = new QHBoxLayout;
    btnAddField    = new QPushButton("Añadir campo");
    btnRemoveField = new QPushButton("Eliminar campo");
    fieldBtns->addWidget(btnAddField);
    fieldBtns->addWidget(btnRemoveField);
    fieldBtns->addStretch();

    // -------- Propiedades abajo --------
    propTabs = new QTabWidget;

    // General
    QWidget *general = new QWidget;
    QFormLayout *fl = new QFormLayout(general);
    propFormato   = new QLineEdit;
    propMascara   = new QLineEdit;
    propTitulo    = new QLineEdit;
    propValorPred = new QLineEdit;
    propReglaVal  = new QLineEdit;
    propTextoVal  = new QLineEdit;
    propRequerido = new QCheckBox("Requerido");
    propIndexado  = new QComboBox;
    propIndexado->addItems({"No", "Sí (con duplicados)", "Sí (sin duplicados)"});

    fl->addRow("Formato:", propFormato);
    fl->addRow("Máscara de entrada:", propMascara);
    fl->addRow("Título:", propTitulo);
    fl->addRow("Valor predeterminado:", propValorPred);
    fl->addRow("Regla de validación:", propReglaVal);
    fl->addRow("Texto de validación:", propTextoVal);
    fl->addRow("Indexado:", propIndexado);
    fl->addRow("", propRequerido);

    // Búsqueda (maqueta)
    QWidget *busqueda = new QWidget;
    QFormLayout *fl2 = new QFormLayout(busqueda);
    fl2->addRow(new QLabel("Propiedades de búsqueda (maqueta visual)"));
    fl2->addRow("Mostrar el selector de fecha:", new QLabel("Para fechas"));

    propTabs->addTab(general,  "General");
    propTabs->addTab(busqueda, "Búsqueda");

    // Ensamblar central
    center->addLayout(topBar);
    center->addWidget(fieldsTable, 1);
    center->addLayout(fieldBtns);
    center->addWidget(propTabs);

    root->addLayout(center, 1);   // <-- ahora solo agregamos el central aquí

    // Conexiones
    connect(tablesList,        &QListWidget::currentRowChanged, this, &TablesPage::onSelectTable);
    connect(btnAddField,       &QPushButton::clicked,           this, &TablesPage::onAddField);
    connect(btnRemoveField,    &QPushButton::clicked,           this, &TablesPage::onRemoveField);
    connect(btnNueva,          &QPushButton::clicked,           this, &TablesPage::onNuevaTabla);
    connect(btnEditar,         &QPushButton::clicked,           this, &TablesPage::onEditarTabla);
    connect(btnEliminar,       &QPushButton::clicked,           this, &TablesPage::onEliminarTabla);
    connect(fieldsTable,       &QTableWidget::itemChanged,      this, &TablesPage::onNameItemEdited);

    connect(fieldsTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &TablesPage::onFieldSelectionChanged);

    // Propiedades -> datos del campo
    connect(propFormato,   &QLineEdit::textEdited,         this, &TablesPage::onPropertyChanged);
    connect(propMascara,   &QLineEdit::textEdited,         this, &TablesPage::onPropertyChanged);
    connect(propTitulo,    &QLineEdit::textEdited,         this, &TablesPage::onPropertyChanged);
    connect(propValorPred, &QLineEdit::textEdited,         this, &TablesPage::onPropertyChanged);
    connect(propReglaVal,  &QLineEdit::textEdited,         this, &TablesPage::onPropertyChanged);
    connect(propTextoVal,  &QLineEdit::textEdited,         this, &TablesPage::onPropertyChanged);
    connect(propRequerido, &QCheckBox::toggled,            this, &TablesPage::onPropertyChanged);
    connect(propIndexado,  &QComboBox::currentTextChanged, this, &TablesPage::onPropertyChanged);

    // NUEVO: guardar descripción en memoria al editar
    connect(tableDescEdit, &QLineEdit::textEdited, this, [=](const QString &txt){
        const auto t = currentTableName();
        if (!t.isEmpty()) tableDesc_[t] = txt;
    });
}


void TablesPage::applyQss() {
    setStyleSheet(R"(

    /* ====== Base (modo Access claro) ====== */
    QWidget { background:#ffffff; color:#222; font:10pt "Segoe UI"; }

    /* Panel izquierdo */
    #lblSideTitle { font-weight:600; color:#444; padding-left:6px; }
    QListWidget {
        background:#f7f7f7; border:1px solid #cfcfcf; border-radius:4px;
    }
    QListWidget::item { padding:6px 10px; }
    QListWidget::item:hover    { background:#ececec; }
    QListWidget::item:selected { background:#d9e1f2; color:#000; }

    /* Entradas y combos de la barra superior */
    QLineEdit, QComboBox, QSpinBox {
        background:#ffffff; border:1px solid #bfbfbf; border-radius:3px; padding:4px 6px;
    }
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border:2px solid #c0504d; } /* rojo foco */

    QPushButton {
        background:#e6e6e6; border:1px solid #bfbfbf; border-radius:3px; padding:6px 12px;
    }
    QPushButton:hover { background:#f2f2f2; }
    QPushButton:pressed { background:#dddddd; }

    /* ====== Tabla (grid de diseño) ====== */
    QTableWidget {
        background:#ffffff; border:1px solid #cfcfcf; border-radius:3px;
        gridline-color:#d0d0d0; alternate-background-color:#fafafa;
    }
    QHeaderView::section {
        background:#fff2cc;  /* encabezado amarillo */
        color:#000; padding:6px 8px; border:1px solid #d6d6d6; font-weight:600;
    }

    /* Selección de filas estilo Access (azul claro) */
    QTableView::item:selected { background:#d9e1f2; color:#000; }
    QTableView::item:hover { background:#f5f7fb; }

    /* Marco rojo cuando la celda tiene foco (sin selección) */
    QTableView::item:active:!selected { outline: 2px solid #c0504d; outline-offset:-1px; }

    /* Editor del combo dentro de celda */
    QComboBox QAbstractItemView {
        background:#ffffff; border:1px solid #bfbfbf; selection-background-color:#d9e1f2;
    }

    /* Tabs de propiedades */
    QTabWidget::pane { border:1px solid #cfcfcf; border-radius:3px; top:-1px; }
    QTabBar::tab { background:#f7f7f7; border:1px solid #cfcfcf; border-bottom:0;
                   padding:6px 10px; margin-right:2px; }
    QTabBar::tab:selected { background:#ffffff; }

    QLabel { color:#333; }
    )");
}

/* ================ Datos de ejemplo ================= */
void TablesPage::setupFakeData() {
    dbMock["Alumno"] = {
        {"IdAlumno", "Autonumeración", 4, true,  "", "", "", "", "", "", true,  "Sí (sin duplicados)"},
        {"Nombre",   "Texto corto",    50,false, "", "", "", "", "", "", true,  "Sí (con duplicados)"},
        {"Correo",   "Texto corto",    80,false, "", "", "", "", "", "", false, "Sí (con duplicados)"}
    };
    dbMock["Clase"] = {
        {"IdClase", "Autonumeración", 4, true, "", "", "", "", "", "", true, "Sí (sin duplicados)"},
        {"Nombre",  "Texto corto",    60,false,"", "", "", "", "", "", true, "Sí (con duplicados)"},
        {"Horario", "Texto corto",    20,false,"", "", "", "", "", "", false,"No"}
    };
    dbMock["Matricula"] = {
        {"IdMatricula",    "Autonumeración", 4, true,  "", "", "", "", "", "", true,  "Sí (sin duplicados)"},
        {"IdAlumno",       "Número",         4, false, "", "", "", "", "", "", true,  "Sí (con duplicados)"},
        {"FechaMatricula", "Fecha/Hora",     8, false, "DD/MM/YY", "", "", "", "", "", true, "No"},
        {"Saldo",          "Moneda",         8, false, "Lps", "", "", "0", "", "", false,"No"},
        {"Observacion",    "Texto corto",    255,false, "", "", "", "", "", "", false,"No"}
    };

    // Descripciones iniciales (opcionales)
    tableDesc_["Alumno"]    = "Catálogo de alumnos de la universidad.";
    tableDesc_["Clase"]     = "Catálogo de clases/assignaturas.";
    tableDesc_["Matricula"] = "Relación de inscripciones por período.";

    auto icon = qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView);

    tablesList->addItem(new QListWidgetItem(icon, "Alumno"));
    tablesList->addItem(new QListWidgetItem(icon, "Clase"));
    tablesList->addItem(new QListWidgetItem(icon, "Matricula"));
}

QString TablesPage::currentTableName() const {
    auto *it = tablesList->currentItem();
    return it ? it->text() : QString();
}

/* ============== Carga y sincronización ============== */
void TablesPage::loadTableToUi(const QString &tableName) {
    const QSignalBlocker blockNames(fieldsTable); // evita reentradas de itemChanged
    fieldsTable->setRowCount(0);
    tableNameEdit->setText(tableName);

    // cargar descripción asociada
    tableDescEdit->setText(tableDesc_.value(tableName));

    const auto &fields = dbMock[tableName];

    for (int i = 0; i < fields.size(); ++i) {
        fieldsTable->insertRow(i);
        buildRowFromField(i, fields[i]);
        connectRowEditors(i);
    }

    if (fieldsTable->rowCount() > 0) {
        fieldsTable->selectRow(0);
    } else {
        clearPropsUi();
    }
}

void TablesPage::buildRowFromField(int row, const FieldDef &fd) {
    // Nombre (editable como item)
    auto *nameItem = new QTableWidgetItem(fd.name);
    nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);
    fieldsTable->setItem(row, 0, nameItem);

    // Tipo (combo)
    auto *typeCb = new QComboBox(fieldsTable);
    typeCb->addItems(kTypes());
    typeCb->setCurrentText(fd.type);
    fieldsTable->setCellWidget(row, 1, typeCb);

    // Tamaño (spin)
    auto *sizeSp = new QSpinBox(fieldsTable);
    sizeSp->setRange(0, 255);
    sizeSp->setValue(fd.size);
    fieldsTable->setCellWidget(row, 2, sizeSp);

    // PK (checkbox dentro de un wrapper para centrar)
    auto *pkChk = new QCheckBox(fieldsTable);
    pkChk->setChecked(fd.pk);
    pkChk->setStyleSheet("margin-left:12px;");
    QWidget *wrap = new QWidget(fieldsTable);
    QHBoxLayout *hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(0,0,0,0);
    hl->addWidget(pkChk);
    hl->addStretch();
    fieldsTable->setCellWidget(row, 3, wrap);
}

void TablesPage::connectRowEditors(int row) {
    auto table = currentTableName();
    if (table.isEmpty()) return;

    // Tipo
    auto *typeCb = qobject_cast<QComboBox*>(fieldsTable->cellWidget(row,1));
    QObject::connect(typeCb, &QComboBox::currentTextChanged, this, [=](const QString &t){
        auto &list = dbMock[table];
        if (row < 0 || row >= list.size()) return;
        list[row].type = t;

        // Sugerir tamaño si está en cero
        auto *sizeSp = qobject_cast<QSpinBox*>(fieldsTable->cellWidget(row,2));
        if (list[row].size == 0) {
            if (t == "Texto corto") sizeSp->setValue(50), list[row].size = 50;
            if (t == "Número")      sizeSp->setValue(4),  list[row].size = 4;
            if (t == "Moneda")      sizeSp->setValue(8),  list[row].size = 8;
            if (t == "Fecha/Hora")  sizeSp->setValue(8),  list[row].size = 8;
        }
    });

    // Tamaño
    auto *sizeSp = qobject_cast<QSpinBox*>(fieldsTable->cellWidget(row,2));
    QObject::connect(sizeSp, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int v){
        auto &list = dbMock[table];
        if (row < 0 || row >= list.size()) return;
        list[row].size = v;
    });

    // PK
    auto *wrap = fieldsTable->cellWidget(row,3);
    auto *pkChk = wrap->findChild<QCheckBox*>();
    QObject::connect(pkChk, &QCheckBox::toggled, this, [=](bool on){
        auto &list = dbMock[table];
        if (row < 0 || row >= list.size()) return;
        list[row].pk = on;
    });
}

void TablesPage::onNameItemEdited(QTableWidgetItem *it) {
    if (!it || it->column() != 0) return;
    auto table = currentTableName();
    if (table.isEmpty()) return;
    auto &list = dbMock[table];
    int row = it->row();
    if (row < 0 || row >= list.size()) return;
    list[row].name = it->text();
}

void TablesPage::onSelectTable() {
    const auto name = currentTableName();
    if (name.isEmpty()) return;
    loadTableToUi(name);
}

void TablesPage::onFieldSelectionChanged() {
    auto table = currentTableName();
    if (table.isEmpty()) { clearPropsUi(); return; }
    auto &list = dbMock[table];
    int row = fieldsTable->currentRow();
    if (row < 0 || row >= list.size()) { clearPropsUi(); return; }
    loadFieldPropsToUi(list[row]);
}

void TablesPage::loadFieldPropsToUi(const FieldDef &fd) {
    QSignalBlocker b1(propFormato), b2(propMascara), b3(propTitulo),
        b4(propValorPred), b5(propReglaVal), b6(propTextoVal),
        b7(propIndexado), b8(propRequerido);
    propFormato->setText(fd.formato);
    propMascara->setText(fd.mascaraEntrada);
    propTitulo->setText(fd.titulo);
    propValorPred->setText(fd.valorPredeterminado);
    propReglaVal->setText(fd.reglaValidacion);
    propTextoVal->setText(fd.textoValidacion);
    propRequerido->setChecked(fd.requerido);
    propIndexado->setCurrentText(fd.indexado);
}

void TablesPage::pullPropsFromUi(FieldDef &fd) {
    fd.formato            = propFormato->text();
    fd.mascaraEntrada     = propMascara->text();
    fd.titulo             = propTitulo->text();
    fd.valorPredeterminado= propValorPred->text();
    fd.reglaValidacion    = propReglaVal->text();
    fd.textoValidacion    = propTextoVal->text();
    fd.requerido          = propRequerido->isChecked();
    fd.indexado           = propIndexado->currentText();
}

void TablesPage::onPropertyChanged() {
    auto table = currentTableName();
    if (table.isEmpty()) return;
    auto &list = dbMock[table];
    int row = fieldsTable->currentRow();
    if (row < 0 || row >= list.size()) return;
    pullPropsFromUi(list[row]);
}

void TablesPage::clearPropsUi() {
    QSignalBlocker b1(propFormato), b2(propMascara), b3(propTitulo),
        b4(propValorPred), b5(propReglaVal), b6(propTextoVal),
        b7(propIndexado), b8(propRequerido);
    propFormato->clear();
    propMascara->clear();
    propTitulo->clear();
    propValorPred->clear();
    propReglaVal->clear();
    propTextoVal->clear();
    propRequerido->setChecked(false);
    propIndexado->setCurrentIndex(0);
}

/* =================== CRUD de campos =================== */
void TablesPage::onAddField() {
    auto table = currentTableName();
    if (table.isEmpty()) return;
    auto &list = dbMock[table];

    FieldDef fd;
    fd.name = "NuevoCampo";
    fd.type = "Texto corto";
    fd.size = 50;
    list.append(fd);

    int r = fieldsTable->rowCount();
    fieldsTable->insertRow(r);
    buildRowFromField(r, fd);
    connectRowEditors(r);
    fieldsTable->selectRow(r);
}

void TablesPage::onRemoveField() {
    auto table = currentTableName();
    if (table.isEmpty()) return;
    auto &list = dbMock[table];
    int row = fieldsTable->currentRow();
    if (row < 0 || row >= list.size()) return;

    list.removeAt(row);
    fieldsTable->removeRow(row);
    if (fieldsTable->rowCount() > 0)
        fieldsTable->selectRow(std::min(row, fieldsTable->rowCount()-1));
    else
        clearPropsUi();
}

/* =================== CRUD de tablas =================== */
bool TablesPage::tableExists(const QString& name) const {
    return dbMock.contains(name);
}

bool TablesPage::isValidTableName(const QString& name) const {
    static const QRegularExpression rx("^[A-Za-z_][A-Za-z0-9_]*$");
    return rx.match(name).hasMatch();
}

void TablesPage::onNuevaTabla() {

    tableNameEdit->clear();
    tableDescEdit->clear();
    fieldsTable->setRowCount(0);
    clearPropsUi();

    QString name = tableNameEdit->text().trimmed();

    // Si no hay nombre, pedirlo
    if (name.isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(
                   this, "Nueva tabla",
                   "Nombre de la tabla:",
                   QLineEdit::Normal, "", &ok).trimmed();
        if (!ok) return; // canceló
    }

    // Validaciones
    if (!isValidTableName(name)) {
        QMessageBox::warning(this, "Nombre inválido",
                             "El nombre debe iniciar con letra o _ y puede contener letras, números y _. ");
        return;
    }
    if (tableExists(name)) {
        QMessageBox::information(this, "Duplicado",
                                 "Ya existe una tabla llamada \"" + name + "\".");
        return;
    }

    // Crear tabla en memoria con un campo PK por defecto
    FieldDef pk;
    pk.name = "Id" + name;             // IdMatricula, IdAlumno, etc.
    pk.type = "Autonumeración";
    pk.size = 4;
    pk.pk   = true;
    pk.requerido = true;
    pk.indexado  = "Sí (sin duplicados)";

    dbMock.insert(name, QList<FieldDef>{ pk });

    // Descripción asociada inicia vacía (se actualizará al teclear)
    tableDesc_[name] = QString();

    // UI: agregar a la lista y seleccionar
    auto icon = qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    tablesList->addItem(new QListWidgetItem(icon, name));
    tablesList->setCurrentRow(tablesList->count() - 1);

    // Refrescar diseñador
    loadTableToUi(name);
    tableNameEdit->setText(name);
    tableDescEdit->clear(); // listo para escribir descripción
    if (fieldsTable->rowCount() > 0) fieldsTable->selectRow(0);


    QMessageBox::information(this, "Nueva tabla",
                             "Se creó la tabla \"" + name + "\" (en memoria).");
}

void TablesPage::onEditarTabla() {
    auto *item = tablesList->currentItem();
    if (!item) return;
    const QString oldName = item->text();

    QString newName = tableNameEdit->text().trimmed();
    if (newName.isEmpty()) {
        QMessageBox::warning(this,"Renombrar","Escribe un nombre válido.");
        return;
    }
    if (!isValidTableName(newName)) {
        QMessageBox::warning(this,"Renombrar","Usa letras/números/_ y no inicies con número.");
        return;
    }
    if (dbMock.contains(newName) && newName != oldName) {
        QMessageBox::warning(this,"Renombrar","Ya existe una tabla con ese nombre.");
        return;
    }

    // mover campos
    auto list = dbMock.take(oldName);
    dbMock[newName] = list;

    // mover descripción
    QString desc = tableDesc_.take(oldName);
    tableDesc_[newName] = desc;

    item->setText(newName);
}

void TablesPage::onEliminarTabla() {
    auto *item = tablesList->currentItem();
    if (!item) return;
    QString name = item->text();
    if (QMessageBox::question(this,"Eliminar",
                              "¿Eliminar la tabla \"" + name + "\"?") != QMessageBox::Yes) return;

    dbMock.remove(name);
    tableDesc_.remove(name);

    delete tablesList->takeItem(tablesList->currentRow());
    fieldsTable->setRowCount(0);
    tableNameEdit->clear();
    tableDescEdit->clear();
    clearPropsUi();
}
