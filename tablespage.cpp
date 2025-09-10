#include "tablespage.h"
#include "datamodel.h"

#include <QVBoxLayout>
#include <QScopeGuard>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QInputDialog>
#include <QRegularExpression>
#include <QApplication>
#include <QStyle>
#include <QTimer>
#include <QSpinBox>
#include <QKeyEvent>
#include <QIntValidator>



/* ===== Helpers ===== */
static QStringList kTypes() {
    // Orden parecido a Access
    return {"Autonumeración","Número","Fecha/Hora","Moneda","Sí/No","Texto corto","Texto largo"};
}

// Igual que en DataModel, pero local a esta vista
QString TablesPage::normType(const QString& t) {
    const QString s = t.trimmed().toLower();
    if (s.startsWith(u"auto"))                                    return "autonumeracion";
    if (s.startsWith(u"número") || s.startsWith(u"numero"))       return "numero";
    if (s.startsWith(u"fecha"))                                   return "fecha_hora";
    if (s.startsWith(u"moneda"))                                  return "moneda";
    if (s.startsWith(u"sí/no") || s.startsWith(u"si/no"))         return "booleano";
    if (s.startsWith(u"texto largo"))                             return "texto_largo";
    return "texto"; // Texto corto
}

// Mostrar/ocultar filas en un QFormLayout (campo+etiqueta)
static void setRowVisible(QFormLayout* fl, QWidget* field, bool vis) {
    if (!fl || !field) return;
    if (auto *lab = fl->labelForField(field)) lab->setVisible(vis);
    field->setVisible(vis);
}

/* ===== Constructor ===== */
TablesPage::TablesPage(QWidget *parent, bool withSidebar)
    : QWidget(parent), withSidebar_(withSidebar) {
    setupUi();
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

    // descripción (opcional)
    tableDescEdit = new QLineEdit;
    tableDescEdit->setPlaceholderText("Descripción de la tabla (opcional)");

    btnNueva    = new QPushButton("Nueva");
    btnEditar   = new QPushButton("Renombrar");
    btnEliminar = new QPushButton("Eliminar");

    topBar->addWidget(new QLabel("Diseñando:"));
    topBar->addWidget(tableNameEdit, 1);
    topBar->addStretch();
    topBar->addWidget(btnEditar);
    topBar->addWidget(btnEliminar);

    // Grid central (3 columnas: Nombre | Tipo | PK)
    fieldsTable = new QTableWidget(0, 3);
    fieldsTable->setHorizontalHeaderLabels({"Nombre del campo", "Tipo de datos", "PK"});
    fieldsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    fieldsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    fieldsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
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

    // Controles visibles
    propFormato = new QComboBox(general);
    propFormato->setEditable(true);
    propFormato->setInsertPolicy(QComboBox::NoInsert);
    if (propFormato->lineEdit()) propFormato->lineEdit()->setPlaceholderText("Formato: 0, 0.00, #,##0.00, etc.");
    propAutoFormato = new QComboBox(general);          // ← lo usamos como **Tamaño**
    propValorPred   = new QLineEdit(general);
    propRequerido   = new QCheckBox("Requerido", general);

    propTextSize    = new QLineEdit(general);
    propTextSize->setValidator(new QIntValidator(1, 255, propTextSize));
    propTextSize->setText(QStringLiteral("255"));
    propTextSize->setPlaceholderText(QStringLiteral("1..255"));
    propTextSize->installEventFilter(this);


    // (Opcionales que ya no se muestran)
    propAutoNewValues = new QComboBox(general); propAutoNewValues->hide();
    propTitulo        = new QLineEdit(general);  propTitulo->hide();
    propIndexado      = new QComboBox(general);  propIndexado->hide();

    // Filas (obs: “Tamaño:”)
    fl->addRow("Formato:", propFormato);      // solo para Autonum y Número
    fl->addRow("Tamaño:",  propAutoFormato);  // Autonum y Número
    fl->addRow("Field size:", propTextSize);
    fl->addRow("Valor predeterminado:", propValorPred);
    fl->addRow("", propRequerido);

    propTabs->addTab(general, "General");

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


    fl->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    // Todos igual de largos (se calcula cuando la UI ya está montada)
    makePropsUniformWidth();

    root->addLayout(center, 1);

    // --- Mantener tamaño constante del panel "General"/propiedades ---
    // Altura fija (ajusta el número a tu gusto: 260–320 suele verse bien)
    const int kPropsFixedH = 280;
    propTabs->setMinimumHeight(kPropsFixedH);
    propTabs->setMaximumHeight(kPropsFixedH);
    propTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Da todo el espacio sobrante al grid de campos
    center->setStretch(1, 1);  // fieldsTable
    center->setStretch(3, 0);  // propTabs

    // Conexiones
    connect(tablesList,        &QListWidget::currentRowChanged, this, &TablesPage::onSelectTable);
    connect(btnAddField,       &QPushButton::clicked,           this, &TablesPage::onAddField);
    connect(btnRemoveField,    &QPushButton::clicked,           this, &TablesPage::onRemoveField);
    connect(btnEditar,         &QPushButton::clicked,           this, &TablesPage::onEditarTabla);
    connect(btnEliminar,       &QPushButton::clicked,           this, &TablesPage::onEliminarTabla);
    connect(fieldsTable,       &QTableWidget::itemChanged,      this, &TablesPage::onNameItemEdited);

    connect(fieldsTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &TablesPage::onFieldSelectionChanged);

    // Propiedades -> datos del campo
    connect(propFormato, &QComboBox::currentTextChanged, this, &TablesPage::onPropertyChanged);
    connect(propAutoFormato,   &QComboBox::currentTextChanged, this, [=]{ updateAutoControlsSensitivity(); onPropertyChanged(); });
    connect(propValorPred, &QLineEdit::editingFinished, this, &TablesPage::onPropertyChanged);
    connect(propTextSize, &QLineEdit::editingFinished, this, &TablesPage::onPropertyChanged);


    connect(propRequerido, &QCheckBox::toggled,            this, &TablesPage::onPropertyChanged);

    // guardar descripción en memoria al editar
    connect(tableDescEdit, &QLineEdit::textEdited, this, [=](const QString &txt){
        const auto t = currentTableName();
        if (!t.isEmpty()) DataModel::instance().setTableDescription(t, txt);
    });
}

void TablesPage::applyQss() {
    setStyleSheet(R"(
    QWidget { background:#ffffff; color:#222; font:10pt "Segoe UI"; }
    #lblSideTitle { font-weight:600; color:#444; padding-left:6px; }
    QListWidget { background:#f7f7f7; border:1px solid #cfcfcf; border-radius:4px; }
    QListWidget::item { padding:6px 10px; }
    QListWidget::item:hover    { background:#ececec; }
    QListWidget::item:selected { background:#d9e1f2; color:#000; }
    QLineEdit, QComboBox {
        background:#ffffff; border:1px solid #bfbfbf; border-radius:3px; padding:4px 6px;
    }
    QLineEdit:focus, QComboBox:focus { border:2px solid #c0504d; }
    QPushButton {
        background:#e6e6e6; border:1px solid #bfbfbf; border-radius:3px; padding:6px 12px;
    }
    QPushButton:hover { background:#f2f2f2; }
    QPushButton:pressed { background:#dddddd; }
    QTableWidget {
        background:#ffffff; border:1px solid #cfcfcf; border-radius:3px;
        gridline-color:#d0d0d0; alternate-background-color:#fafafa;
    }
    QHeaderView::section {
        background:#fff2cc; color:#000; padding:6px 8px; border:1px solid #d6d6d6; font-weight:600;
    }
    QTableView::item:selected { background:#d9e1f2; color:#000; }
    QTableView::item:hover { background:#f5f7fb; }
    QComboBox QAbstractItemView {
        background:#ffffff; border:1px solid #bfbfbf; selection-background-color:#d9e1f2;
    }
    QTabWidget::pane { border:1px solid #cfcfcf; border-radius:3px; top:-1px; }
    QTabBar::tab { background:#f7f7f7; border:1px solid #cfcfcf; border-bottom:0;
                   padding:6px 10px; margin-right:2px; }
    QTabBar::tab:selected { background:#ffffff; }
    QLabel { color:#333; }
    )");
}

/* ================ Datos de ejemplo ================= */



void TablesPage::updateTablesList(const QString& preferSelect)
{
    const QString cur   = currentTableName();
    const QStringList names = DataModel::instance().tables();
    const QIcon icon = qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView);

    // Bloquear solo mientras repoblamos
    {
        QSignalBlocker b(tablesList);
        tablesList->clear();
        for (const auto& n : names) {
            tablesList->addItem(new QListWidgetItem(icon, n));
        }
    } // ← desde aquí vuelven a emitirse señales

    QString target = preferSelect.isEmpty() ? cur : preferSelect;
    int idx = names.indexOf(target);
    if (idx < 0 && !names.isEmpty()) idx = 0;

    if (idx >= 0) {
        const bool changed = (idx != tablesList->currentRow());
        tablesList->setCurrentRow(idx);        // ahora SÍ emite currentRowChanged
        if (!changed) onSelectTable();         // asegura refresco del encabezado
    }
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

    if (fieldsTable->rowCount() > 0) fieldsTable->selectRow(0);
    else                              clearPropsUi();
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

    // PK (checkbox centrado)
    auto *pkChk = new QCheckBox(fieldsTable);
    pkChk->setChecked(fd.pk);
    pkChk->setStyleSheet("margin-left:12px;");
    QWidget *wrap = new QWidget(fieldsTable);
    QHBoxLayout *hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(0,0,0,0);
    hl->addWidget(pkChk);
    hl->addStretch();
    fieldsTable->setCellWidget(row, 2, wrap);
}

bool TablesPage::applySchemaAndRefresh(const Schema& s, int preserveRow)
{
    if (m_currentTable.isEmpty()) return false;

    QString err;
    if (!DataModel::instance().setSchema(m_currentTable, s, &err)) {
        QMessageBox::warning(this, tr("Esquema inválido"), err);
        return false;
    }

    const int row = (preserveRow >= 0) ? preserveRow : fieldsTable->currentRow();

    // Bloquea eventos de selección durante la recarga para evitar saltar a la fila 0
    {
        QSignalBlocker bSel(fieldsTable->selectionModel());
        loadTableToUi(m_currentTable);               // reconstruye grilla sin selectionChanged
        if (row >= 0 && row < fieldsTable->rowCount())
            fieldsTable->selectRow(row);             // re-selecciona la fila del usuario
    }

    // Refresca el panel para la fila realmente seleccionada
    m_currentSchema = DataModel::instance().schema(m_currentTable);
    if (row >= 0 && row < m_currentSchema.size()) {
        loadFieldPropsToUi(m_currentSchema[row]);
        updateGeneralUiForType(m_currentSchema[row].type);
    } else {
        clearPropsUi();
    }

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
        updateGeneralUiForType(t);     // Actualiza panel General en caliente
        applySchemaAndRefresh(s, row);
    });

    // PK (columna 2)
    auto *wrap = fieldsTable->cellWidget(row,2);
    auto *pkChk = wrap->findChild<QCheckBox*>();
    QObject::connect(pkChk, &QCheckBox::toggled, this, [=](bool on){
        if (row < 0 || row >= m_currentSchema.size()) return;
        Schema s = m_currentSchema;
        s[row].pk = on;
        applySchemaAndRefresh(s, row);
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
    if (m_updatingUi) return;
    int row = fieldsTable->currentRow();
    if (row < 0 || row >= m_currentSchema.size()) { clearPropsUi(); return; }
    loadFieldPropsToUi(m_currentSchema[row]);
    updateGeneralUiForType(m_currentSchema[row].type);   // Ajusta visibilidad por tipo
}

void TablesPage::loadFieldPropsToUi(const FieldDef &fd) {
    QSignalBlocker b1(propFormato), b3(propTitulo),
        b4(propValorPred), b7(propIndexado), b8(propRequerido);
    propFormato->setCurrentText(fd.formato);
    propAutoFormato->setCurrentText(fd.autoSubtipo);
    propAutoFormato->setCurrentText(fd.autoSubtipo.isEmpty() ? "Long Integer" : fd.autoSubtipo);
    propAutoNewValues->setCurrentText(fd.autoNewValues.isEmpty() ? "Increment" : fd.autoNewValues);

    // Field size (solo aplica a Texto corto)
    if (normType(fd.type) == "texto") {
        // Texto corto: size en propTextSize (QLineEdit)
        const int sz = (fd.size <= 0 || fd.size > 255) ? 255 : fd.size;
        propTextSize->setText(QString::number(sz));    }
    propTitulo->setText(fd.titulo);
    propValorPred->setText(fd.valorPredeterminado);
    propRequerido->setChecked(fd.requerido);
    propIndexado->setCurrentText(fd.indexado);

    // Placeholders “a lo Access” según tipo
    const QString t = normType(fd.type);
    if (t == "autonumeracion") {
        QString fmt = fd.formato.trimmed().isEmpty() ? QStringLiteral("General Number") : fd.formato;
        propFormato->setCurrentText(fmt);
        if (propFormato->findText(fmt) < 0) propFormato->addItem(fmt);

    } else if (t == "texto") {
        propFormato->setPlaceholderText("Presentación opcional (no obligatorio)");
    } else if (t == "texto_largo") {
        if (propFormato->lineEdit()) propFormato->lineEdit()->setPlaceholderText("Texto sin límite fijo");

    } else if (t == "numero") {
        if (propFormato->lineEdit()) propFormato->lineEdit()->setPlaceholderText("Formato/decimales: Auto, 0, 0.00, #,##0.00");

    } else if (t == "moneda") {
        propFormato->setPlaceholderText("Lps / $ / €  (p.ej. L #,##0.00)");

    } else if (t == "fecha_hora") {
        if (propFormato->lineEdit()) propFormato->lineEdit()->setPlaceholderText("dd/MM/yy, yyyy-MM-dd, etc.");

    } else if (t == "booleano") {
        propFormato->setPlaceholderText("Sí/No (o Verdadero/Falso)");

    }
}

void TablesPage::pullPropsFromUi(FieldDef &fd) {
    fd.formato            = propFormato->currentText();
    // Solo tiene sentido en Autonumeración, pero guardar no hace daño
    fd.autoSubtipo        = propAutoFormato->currentText();
    fd.autoNewValues      = propAutoNewValues->currentText();
    fd.titulo             = propTitulo->text();
    fd.valorPredeterminado= propValorPred->text();
    fd.requerido          = propRequerido->isChecked();
    fd.indexado           = propIndexado->currentText();

    // Tamaño aplica solo a Texto corto
    if (normType(fd.type) == "texto") {
        bool ok = false;
        int v = propTextSize->text().toInt(&ok);
        if (!ok) v = 255;
        fd.size = std::max(1, std::min(v, 255));
        }
}

// #include <QTimer>
void TablesPage::onPropertyChanged() {
    if (m_updatingUi) return;
    const int row = fieldsTable->currentRow();
    if (row < 0 || row >= m_currentSchema.size()) return;

    // Recoge cambios actuales de la UI
    Schema s = m_currentSchema;
    pullPropsFromUi(s[row]);  // ya existe :contentReference[oaicite:6]{index=6}

    // Recuerda dónde estaba el foco para restaurarlo luego
    QWidget* hadFocus = QApplication::focusWidget();

    // Encola la recarga para evitar destruir widgets durante la señal
    QTimer::singleShot(0, this, [=]{
        applySchemaAndRefresh(s, row);         // recarga todo :contentReference[oaicite:7]{index=7}
        if (hadFocus == propValorPred && propValorPred) propValorPred->setFocus();
        else if (hadFocus == propFormato && propFormato) {
            if (auto *le = propFormato->lineEdit()) le->setFocus();
            else propFormato->setFocus();
        } else if (hadFocus == propTextSize && propTextSize) propTextSize->setFocus();
    });
}


void TablesPage::clearPropsUi() {
    QSignalBlocker b1(propFormato), b3(propTitulo),
        b4(propValorPred), b7(propIndexado), b8(propRequerido);
    propFormato->setCurrentText("");
    propTitulo->clear();
    propValorPred->clear();
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
    fd.size = 255; // el modelo lo usará si aplica; no se muestra en la UI
    s.append(fd);

    applySchemaAndRefresh(s, s.size()-1);
}

void TablesPage::onRemoveField() {
    if (m_currentTable.isEmpty()) return;
    int row = fieldsTable->currentRow();
    if (row < 0 || row >= m_currentSchema.size()) return;

    Schema s = m_currentSchema;
    s.removeAt(row);
    applySchemaAndRefresh(s, qMin(row, s.size()-1));
}

void TablesPage::onNuevaTabla() {
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Nueva tabla"),
                                         tr("Nombre de la nueva tabla:"), QLineEdit::Normal,
                                         "NuevaTabla", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    if (!isValidTableName(name)) {
        QMessageBox::warning(this, tr("Nombre inválido"),
                             tr("El nombre debe iniciar con letra o guion bajo y sólo contener letras, números o _."));
        return;
    }

    Schema s;
    QString err;
    if (!DataModel::instance().createTable(name, s, &err)) {
        QMessageBox::warning(this, tr("Crear tabla"), err);
        return;
    }
    updateTablesList(name);
}

void TablesPage::onEditarTabla() {
    const QString cur = currentTableName();
    if (cur.isEmpty()) return;

    bool ok = false;
    QString newName = QInputDialog::getText(this, tr("Renombrar tabla"),
                                            tr("Nuevo nombre:"), QLineEdit::Normal,
                                            cur, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName == cur) return;

    QString err;
    if (!DataModel::instance().renameTable(cur, newName, &err)) {
        QMessageBox::warning(this, tr("Renombrar"), err);
        return;
    }
    updateTablesList(newName);
}

void TablesPage::onEliminarTabla() {
    const QString cur = currentTableName();
    if (cur.isEmpty()) return;
    if (QMessageBox::question(this, tr("Eliminar"),
                              tr("¿Eliminar la tabla \"%1\"?").arg(cur)) != QMessageBox::Yes)
        return;

    QString err;
    if (!DataModel::instance().dropTable(cur, &err)) {
        QMessageBox::warning(this, tr("Eliminar"), err);
        return;
    }
    updateTablesList();
}

/* =================== validación =================== */
bool TablesPage::isValidTableName(const QString& name) const {
    static const QRegularExpression rx("^[A-Za-z_][A-Za-z0-9_]*$");
    return rx.match(name).hasMatch();
}


void TablesPage::updateGeneralUiForType(const QString& type)
{
    m_updatingUi = true;
    const auto guard = qScopeGuard([this]{ m_updatingUi = false; });
    QSignalBlocker bAF(propAutoFormato);
    QSignalBlocker bANV(propAutoNewValues);
    auto *general = propTabs->widget(0);
    auto *fl = qobject_cast<QFormLayout*>(general->layout());
    if (!fl) return;

    const QString t = normType(type);

    // ¿El campo seleccionado es PK? (para ocultar “Requerido” en PK)
    bool isPk = false;
    int row = fieldsTable ? fieldsTable->currentRow() : -1;
    if (row >= 0 && row < m_currentSchema.size()) isPk = m_currentSchema[row].pk;

    // Oculta todo por defecto
    setRowVisible(fl, propFormato,     false);
    setRowVisible(fl, propAutoFormato, false);
    setRowVisible(fl, propValorPred,   false);
    setRowVisible(fl, propRequerido,   false);
    setRowVisible(fl, propTextSize,    false);

    // Resetea combos
    if (propFormato)       { propFormato->clear(); propFormato->clearEditText(); }
    if (propAutoFormato)   { propAutoFormato->clear(); propAutoFormato->setEnabled(true); }
    if (propAutoNewValues) propAutoNewValues->hide(); // no se usa visualmente

    // Mostrar según tipo
    if (t == "autonumeracion") {
        // Tamaño = Long Integer fijo
        propAutoFormato->addItem("Long Integer");
        propAutoFormato->setCurrentIndex(0);
        propAutoFormato->setEnabled(false);

        // Formato (presentación) + Tamaño
        setRowVisible(fl, propFormato,     true);
        setRowVisible(fl, propAutoFormato, true);
        propFormato->setEditable(false);
        propFormato->clear();
        propFormato->addItems({"General Number","Currency","Euro","Fixed",
                               "Standard","Percent","Scientific"});
        {
            QString fmt;
            int r = fieldsTable ? fieldsTable->currentRow() : -1;
            if (r >= 0 && r < m_currentSchema.size()) fmt = m_currentSchema[r].formato.trimmed();
            if (fmt.isEmpty()) fmt = QStringLiteral("General Number");
            if (propFormato->findText(fmt) < 0) propFormato->addItem(fmt);
            propFormato->setCurrentText(fmt);
        }
        if (!isPk) setRowVisible(fl, propRequerido, false); // PK siempre no requerido
        return;
    }

    if (t == "numero") {
        propFormato->setEditable(true);
        // Tamaño seleccionable
        propAutoFormato->addItems({"Byte","Integer","Long Integer","Single","Double","Decimal"});
        propAutoFormato->setCurrentText("Long Integer");

        setRowVisible(fl, propFormato,     true);  // presentación
        setRowVisible(fl, propAutoFormato, true);  // tamaño
        propFormato->addItems({"General Number","Currency","Euro","Fixed",
                               "Standard","Percent","Scientific"});
        if (!isPk) setRowVisible(fl, propRequerido, true);
        return;
    }

    if (t == "texto") {
        propFormato->setEditable(true);
        // Solo aplica tamaño de texto (Field size) + valor predeterminado
        setRowVisible(fl, propTextSize,    true);
        setRowVisible(fl, propValorPred,   true);
        if (!isPk) setRowVisible(fl, propRequerido, true);

        // Ajusta el label
        if (auto *lbl = qobject_cast<QLabel*>(fl->labelForField(propTextSize)))
            lbl->setText("Field size:");

        // PONER EL NÚMERO EN EL QLINEEDIT (¡ya no setValue!)
        int idx = fieldsTable ? fieldsTable->currentRow() : -1;
        if (idx >= 0 && idx < m_currentSchema.size()) {
            int sz = m_currentSchema[idx].size > 0 ? m_currentSchema[idx].size : 255;
            if (sz < 1)   sz = 1;
            if (sz > 255) sz = 255;
            propTextSize->setText(QString::number(sz));
        } else {
            propTextSize->setText(QStringLiteral("255"));
        }
        return;
    }

    if (t == "fecha_hora") {
        propFormato->setEditable(true);
        setRowVisible(fl, propFormato,     true);
        propFormato->addItems({"General Date","Long Date","Medium Date","Short Date",
                               "Long Time","Medium Time","Short Time"});
        if (!isPk) setRowVisible(fl, propRequerido, true);
        return;
    }

    if (t == "moneda" || t == "booleano" || t == "texto_largo") {
        propFormato->setEditable(true);
        setRowVisible(fl, propValorPred,   true);
        if (!isPk) setRowVisible(fl, propRequerido, true);
        return;
    }
}

void TablesPage::updateAutoControlsSensitivity() {
    const bool isLong =
        propAutoFormato->currentText().startsWith("Long", Qt::CaseInsensitive);
    propAutoNewValues->setEnabled(isLong);
}

void TablesPage::makePropsUniformWidth()
{
    // Ajusta este valor a tu gusto (p. ej. 360–460)
    const int targetW = 420;

    QList<QWidget*> fields = {
        propFormato, propAutoFormato, propAutoNewValues,
        propTitulo, propValorPred, propIndexado,
        propTextSize
        // (Requerido queda fuera para que no se vea raro)
    };

    for (auto *w : fields) {
        if (!w) continue;
        w->setMinimumWidth(targetW);
        w->setMaximumWidth(targetW);
        w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
}

bool TablesPage::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == propTextSize && ev->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            onPropertyChanged();
            return true; // no dejes que el grid cambie de fila
        }
    }
    return QWidget::eventFilter(obj, ev);
}




