#include "tablespage.h"
#include "datamodel.h"     // <--- necesario para usar DataModel

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
    setupFakeData();     // ahora crea datos en DataModel
    applyQss();

    // Poblar lista desde DataModel
    updateTablesList();

    // Reaccionar a cambios globales en el modelo
    auto& dm = DataModel::instance();
    connect(&dm, &DataModel::tableCreated,  this, [=](const QString& n){ updateTablesList(n); });
    connect(&dm, &DataModel::tableDropped,  this, [=](const QString&){ updateTablesList(); });
    connect(&dm, &DataModel::schemaChanged, this, [=](const QString& n, const Schema& s){
        if (n == m_currentTable) {
            m_currentSchema = s;
            loadTableToUi(n);
        }
        // compatibilidad con ShellWindow
        emit schemaChanged(n, s);
    });

    if (tablesList->count() > 0) {
        tablesList->setCurrentRow(0);
        onSelectTable();
    }
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

    // descripción
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

    root->addLayout(center, 1);

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

    // guardar descripción en memoria al editar
    connect(tableDescEdit, &QLineEdit::textEdited, this, [=](const QString &txt){
        const auto t = currentTableName();
        if (!t.isEmpty()) DataModel::instance().setTableDescription(t, txt);
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
    auto& dm = DataModel::instance();

    // Si ya hay tablas (p.ej. por recarga), no dupliques
    if (!dm.tables().isEmpty()) return;

    Schema alumno = {
        {"IdAlumno", "Autonumeración", 4, true,  "", "", "", "", "", "", true,  "Sí (sin duplicados)"},
        {"Nombre",   "Texto corto",    50,false, "", "", "", "", "", "", true,  "Sí (con duplicados)"},
        {"Correo",   "Texto corto",    80,false, "", "", "", "", "", "", false, "Sí (con duplicados)"}
    };
    Schema clase = {
        {"IdClase", "Autonumeración", 4, true, "", "", "", "", "", "", true, "Sí (sin duplicados)"},
        {"Nombre",  "Texto corto",    60,false,"", "", "", "", "", "", true, "Sí (con duplicados)"},
        {"Horario", "Texto corto",    20,false,"", "", "", "", "", "", false,"No"}
    };
    Schema matricula = {
        {"IdMatricula",    "Autonumeración", 4, true,  "", "", "", "", "", "", true,  "Sí (sin duplicados)"},
        {"IdAlumno",       "Número",         4, false, "", "", "", "", "", "", true,  "Sí (con duplicados)"},
        {"FechaMatricula", "Fecha/Hora",     8, false, "DD/MM/YY", "", "", "", "", "", true, "No"},
        {"Saldo",          "Moneda",         8, false, "Lps", "", "", "0", "", "", false,"No"},
        {"Observacion",    "Texto corto",    255,false, "", "", "", "", "", "", false,"No"}
    };

    QString err;
    dm.createTable("Alumno", alumno, &err);
    dm.createTable("Clase", clase, &err);
    dm.createTable("Matricula", matricula, &err);

    DataModel::instance().setTableDescription("Alumno",    "Catálogo de alumnos de la universidad.");
    DataModel::instance().setTableDescription("Clase",     "Catálogo de clases/asignaturas.");
    DataModel::instance().setTableDescription("Matricula", "Relación de inscripciones por período.");

}

void TablesPage::updateTablesList(const QString& preferSelect) {
    const QString cur = currentTableName();
    const QStringList names = DataModel::instance().tables();
    const QIcon icon = qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView);

    QSignalBlocker b(tablesList);
    tablesList->clear();
    for (const auto& n : names) {
        auto *it = new QListWidgetItem(icon, n);
        tablesList->addItem(it);
    }

    QString target = preferSelect.isEmpty() ? cur : preferSelect;
    int idx = names.indexOf(target);
    if (idx < 0 && !names.isEmpty()) idx = 0;
    if (idx >= 0) tablesList->setCurrentRow(idx);
}

QString TablesPage::currentTableName() const {
    auto *it = tablesList->currentItem();
    return it ? it->text() : QString();
}

/* ============== Carga y sincronización ============== */
void TablesPage::loadTableToUi(const QString &tableName) {
    const QSignalBlocker blockNames(fieldsTable); // evita reentradas
    fieldsTable->setRowCount(0);

    m_currentTable  = tableName;
    m_currentSchema = DataModel::instance().schema(tableName);

    tableNameEdit->setText(tableName);
    tableDescEdit->setText(DataModel::instance().tableDescription(tableName));

    for (int i = 0; i < m_currentSchema.size(); ++i) {
        fieldsTable->insertRow(i);
        buildRowFromField(i, m_currentSchema[i]);
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

    // PK (checkbox centrado)
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

bool TablesPage::applySchemaAndRefresh(const Schema& s, int preserveRow) {
    if (m_currentTable.isEmpty()) return false;

    QString err;
    if (!DataModel::instance().setSchema(m_currentTable, s, &err)) {
        QMessageBox::warning(this, tr("Esquema inválido"), err);
        return false;
    }
    // se normalizó en el modelo; recarga UI
    int row = preserveRow >= 0 ? preserveRow : fieldsTable->currentRow();
    m_currentSchema = DataModel::instance().schema(m_currentTable);
    loadTableToUi(m_currentTable);
    if (row >= 0 && row < fieldsTable->rowCount())
        fieldsTable->selectRow(row);

    // compatibilidad con ShellWindow
    emit schemaChanged(m_currentTable, m_currentSchema);
    return true;
}

void TablesPage::connectRowEditors(int row) {
    // Tipo
    auto *typeCb = qobject_cast<QComboBox*>(fieldsTable->cellWidget(row,1));
    QObject::connect(typeCb, &QComboBox::currentTextChanged, this, [=](const QString &t){
        if (row < 0 || row >= m_currentSchema.size()) return;
        Schema s = m_currentSchema;
        s[row].type = t;

        // Sugerir tamaño si está en cero
        auto *sizeSp = qobject_cast<QSpinBox*>(fieldsTable->cellWidget(row,2));
        if (s[row].size == 0) {
            if (t == "Texto corto") { sizeSp->setValue(50); s[row].size = 50; }
            if (t == "Número")      { sizeSp->setValue(4);  s[row].size = 4; }
            if (t == "Moneda")      { sizeSp->setValue(8);  s[row].size = 8; }
            if (t == "Fecha/Hora")  { sizeSp->setValue(8);  s[row].size = 8; }
        }

        applySchemaAndRefresh(s, row);
    });

    // Tamaño
    auto *sizeSp = qobject_cast<QSpinBox*>(fieldsTable->cellWidget(row,2));
    QObject::connect(sizeSp, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int v){
        if (row < 0 || row >= m_currentSchema.size()) return;
        Schema s = m_currentSchema;
        s[row].size = v;
        applySchemaAndRefresh(s, row);
    });

    // PK
    auto *wrap = fieldsTable->cellWidget(row,3);
    auto *pkChk = wrap->findChild<QCheckBox*>();
    QObject::connect(pkChk, &QCheckBox::toggled, this, [=](bool on){
        if (row < 0 || row >= m_currentSchema.size()) return;
        Schema s = m_currentSchema;
        s[row].pk = on;
        applySchemaAndRefresh(s, row); // fallará si hay >1 PK y revertirá en recarga
    });
}

void TablesPage::onNameItemEdited(QTableWidgetItem *it) {
    if (!it || it->column() != 0) return;
    int row = it->row();
    if (row < 0 || row >= m_currentSchema.size()) return;
    Schema s = m_currentSchema;
    s[row].name = it->text();
    applySchemaAndRefresh(s, row);
}

void TablesPage::onSelectTable() {
    const auto name = currentTableName();
    if (name.isEmpty()) return;
    loadTableToUi(name);
    emit tableSelected(name);
}

void TablesPage::onFieldSelectionChanged() {
    int row = fieldsTable->currentRow();
    if (row < 0 || row >= m_currentSchema.size()) { clearPropsUi(); return; }
    loadFieldPropsToUi(m_currentSchema[row]);
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
    int row = fieldsTable->currentRow();
    if (row < 0 || row >= m_currentSchema.size()) return;
    Schema s = m_currentSchema;
    pullPropsFromUi(s[row]);
    applySchemaAndRefresh(s, row);
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
    if (m_currentTable.isEmpty()) return;
    Schema s = m_currentSchema;

    FieldDef fd;
    fd.name = "NuevoCampo";
    fd.type = "Texto corto";
    fd.size = 50;
    s.append(fd);

    applySchemaAndRefresh(s, s.size()-1);
}

void TablesPage::onRemoveField() {
    if (m_currentTable.isEmpty()) return;
    int row = fieldsTable->currentRow();
    if (row < 0 || row >= m_currentSchema.size()) return;

    Schema s = m_currentSchema;
    s.removeAt(row);

    // IMPORTANTE: castear size() a int y manejar lista vacía
    const int newSel = s.isEmpty() ? -1 : std::min(row, int(s.size()) - 1);
    applySchemaAndRefresh(s, newSel);
}

/* =================== CRUD de tablas =================== */

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
    if (DataModel::instance().tables().contains(name)) {
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

    QString err;
    if (!DataModel::instance().createTable(name, Schema{ pk }, &err)) {
        QMessageBox::warning(this, "Nueva tabla", err);
        return;
    }

    // Descripción asociada inicia vacía (se actualizará al teclear)

    updateTablesList(name);
    loadTableToUi(name);
    tableNameEdit->setText(name);
    tableDescEdit->clear();
    if (fieldsTable->rowCount() > 0) fieldsTable->selectRow(0);

    emit tableSelected(name);
    emit schemaChanged(name, DataModel::instance().schema(name));

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
    if (DataModel::instance().tables().contains(newName) && newName != oldName) {
        QMessageBox::warning(this,"Renombrar","Ya existe una tabla con ese nombre.");
        return;
    }

    QString err;
    if (!DataModel::instance().renameTable(oldName, newName, &err)) {
        QMessageBox::warning(this,"Renombrar", err);
        return;
    }

    // mover descripción local

    updateTablesList(newName);
    loadTableToUi(newName);

    emit tableSelected(newName);
    emit schemaChanged(newName, DataModel::instance().schema(newName));
}

void TablesPage::onEliminarTabla() {
    auto *item = tablesList->currentItem();
    if (!item) return;
    QString name = item->text();
    if (QMessageBox::question(this,"Eliminar",
                              "¿Eliminar la tabla \"" + name + "\"?") != QMessageBox::Yes) return;

    QString err;
    if (!DataModel::instance().dropTable(name, &err)) {
        QMessageBox::warning(this,"Eliminar", err);
        return;
    }


    updateTablesList();
    fieldsTable->setRowCount(0);
    tableNameEdit->clear();
    tableDescEdit->clear();
    clearPropsUi();

    emit schemaChanged(name, {}); // esquema vacío tras eliminar
}
