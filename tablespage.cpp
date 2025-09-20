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
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStandardItem>



/* ===== Helpers ===== */

// ¿Existe ya una fila con Autonumeración? Devuelve su índice o -1.
int TablesPage::autonumRow() const {
    for (int r = 0; r < m_currentSchema.size(); ++r) {
        if (normType(m_currentSchema[r].type) == "autonumeracion")
            return r;
    }
    return -1;
}

// Devuelve el QComboBox de tipo de una fila (columna 1)
QComboBox* TablesPage::typeComboAt(int row) const {
    return qobject_cast<QComboBox*>(fieldsTable->cellWidget(row, 1));
}

// Encuentra el índice del item "Autonumeración" dentro de un combo
int TablesPage::indexOfAutonum(QComboBox* cb) const {
    if (!cb) return -1;
    for (int i = 0; i < cb->count(); ++i) {
        if (normType(cb->itemText(i)) == "autonumeracion") return i;
    }
    return -1;
}

void TablesPage::refreshAutonumLocks() {
    // 1) Lee el esquema “live” del modelo (fuente de verdad)
    const Schema live = DataModel::instance().schema(m_currentTable);

    int autoR = -1;
    for (int r = 0; r < live.size(); ++r) {
        if (normType(live[r].type) == "autonumeracion") { autoR = r; break; }
    }

    for (int r = 0; r < fieldsTable->rowCount(); ++r) {
        if (QComboBox* cb = typeComboAt(r)) {
            const int iAuto = indexOfAutonum(cb);
            if (iAuto < 0) continue;

            // Trabaja sobre el modelo real del combo
            if (auto *sm = qobject_cast<QStandardItemModel*>(cb->model())) {
                const QModelIndex mi = sm->index(iAuto, cb->modelColumn(), cb->rootModelIndex());
                if (!mi.isValid()) continue;
                if (QStandardItem *it = sm->itemFromIndex(mi)) {
                    // Habilita por defecto
                    it->setFlags(it->flags() | Qt::ItemIsEnabled);
                    it->setToolTip(QString());

                    // Si existe una autonum en OTRA fila, deshabilita aquí
                    if (autoR >= 0 && r != autoR) {
                        it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
                        it->setToolTip(tr("Solo un campo Autonumeración por tabla."));
                    }
                }
            } else {
                // Fallback por si algún día cambias el modelo del combo:
                // truco con data roles para algunos estilos (no 100% confiable)
                cb->model()->setData(cb->model()->index(iAuto, 0), 1, Qt::UserRole - 1);
                cb->setItemData(iAuto, QVariant(), Qt::ToolTipRole);
                if (autoR >= 0 && r != autoR) {
                    cb->model()->setData(cb->model()->index(iAuto, 0), 0, Qt::UserRole - 1);
                    cb->setItemData(iAuto, tr("Solo un campo Autonumeración por tabla."), Qt::ToolTipRole);
                }
            }
        }
    }
}


// -- Helper UI: configura el combo de decimales según si es entero o no
// Ajusta el combo "Decimal places" según si el tamaño es entero o decimal.
// preferredDp: si no es entero y tienes un dp guardado (1..4), úsalo; si no, default=2.
static void tuneDecimalPlacesUi(QComboBox* cb, bool isInt, int preferredDp = -1)
{
    QSignalBlocker b(cb);
    const QString prev = cb->currentText();
    cb->clear();

    if (isInt) {
        cb->addItem("0");
        cb->setCurrentText("0");
        cb->setEnabled(false);
        cb->setToolTip(QObject::tr("Entero: los decimales son 0"));
        return;
    }

    cb->addItems({"1","2","3","4"});                 // sin "0" en decimales
    int dp = preferredDp;                            // 1..4 esperado
    if (dp < 1 || dp > 4) {
        // si no hay preferido, intenta conservar el anterior; si no, 2
        if (prev == "1" || prev == "2" || prev == "3" || prev == "4")
            dp = prev.toInt();
        else
            dp = 2;
    }
    cb->setCurrentText(QString::number(dp));
    cb->setEnabled(true);
    cb->setToolTip(QObject::tr("Decimales para números con fracción"));
}



static int parseDecPlaces(const QString& fmt) {
    QRegularExpression rx("\\bdp=(\\d)\\b");
    auto m = rx.match(fmt);
    int dp = m.hasMatch() ? m.captured(1).toInt() : 2;
    return std::clamp(dp, 0, 4);
}
static QString baseFormatKey(const QString& fmt) {
    const int i = fmt.indexOf("|dp=");
    return (i >= 0) ? fmt.left(i).trimmed() : fmt.trimmed();
}

static QStringList kTypes() {
    // Orden parecido a Access
    return {"Autonumeración","Número","Fecha","Moneda","Sí/No","Texto corto","Texto largo"};
}

// === Helpers de nombres (case-insensitive) ===
static bool nameExistsCI(const Schema& s, const QString& name, int exceptRow = -1) {
    const QString key = name.trimmed().toLower();
    if (key.isEmpty()) return false;
    for (int i = 0; i < s.size(); ++i) {
        if (i == exceptRow) continue;
        if (s[i].name.trimmed().toLower() == key) return true;
    }
    return false;
}

static QString genUniqueName(const Schema& s, const QString& base) {
    const QString raw = base.trimmed().isEmpty() ? QStringLiteral("Campo") : base.trimmed();
    QString candidate = raw;
    int n = 1;
    while (nameExistsCI(s, candidate)) {
        candidate = raw + QString::number(n++);
    }
    return candidate;
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

    // --- Decimal places (para Número) ---
    propDecimalPlaces = new QComboBox(general);
    propDecimalPlaces->addItems({"0","1","2","3","4"});
    propDecimalPlaces->setCurrentText("2");
    propDecimalPlaces->setEditable(false);


    if (propFormato->lineEdit()) propFormato->lineEdit()->setPlaceholderText("Formato: 0, 0.00, #,##0.00, etc.");

    propAutoFormato = new QComboBox(general);          // ← lo usamos como **Tamaño**
    propRequerido   = new QCheckBox("Requerido", general);

    propAutoNewValues = new QComboBox(general);
    propAutoNewValues->addItems({ "Increment" });

    propTextSize    = new QLineEdit(general);
    propTextSize->setValidator(new QIntValidator(1, 255, propTextSize));
    propTextSize->setText(QStringLiteral("255"));
    propTextSize->setPlaceholderText(QStringLiteral("1..255"));
    propTextSize->installEventFilter(this);


    // (Opcionales que ya no se muestran)
    propTitulo        = new QLineEdit(general);  propTitulo->hide();
    propIndexado      = new QComboBox(general);  propIndexado->hide();

    // Filas (obs: “Tamaño:”)
    fl->addRow("Formato:", propFormato);      // solo para Autonum y Número
    fl->addRow("Tamaño:",  propAutoFormato);  // Autonum y Número
    fl->addRow("New values:", propAutoNewValues);   // ← NUEVO (solo visible en Autonum)
    fl->addRow("Decimal places:", propDecimalPlaces);

    fl->addRow("Field size:", propTextSize);
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
    // Conexiones
    connect(propDecimalPlaces, &QComboBox::currentTextChanged,
            this, &TablesPage::onPropertyChanged);

    connect(propAutoNewValues, &QComboBox::currentTextChanged,
            this, &TablesPage::onPropertyChanged);
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
    background:#ffffff;
    border:1px solid #bfbfbf;
    color:#000;                             /* ← texto negro */
    selection-background-color:#d9e1f2;     /* ← fondo selección */
    selection-color:#000;                   /* ← texto negro al seleccionar */
    font-family: "Menlo","Consolas","Courier New",monospace; /* ← monoespaciada para alinear */
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

    // >>> FIX: si no hay tablas, limpia todo el panel
    if (names.isEmpty()) {
        m_currentTable.clear();
        m_currentSchema.clear();

        tableNameEdit->clear();
        tableDescEdit->clear();

        {
            QSignalBlocker b(fieldsTable);
            fieldsTable->clearContents();
            fieldsTable->setRowCount(0);
        }

        clearPropsUi();            // deja el panel “General” vacío
        return;                    // no sigas, no hay nada que seleccionar
    }
    // <<< FIN FIX

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

    if (fieldsTable->rowCount() > 0) {
        int sel = (m_activeRow >= 0 && m_activeRow < fieldsTable->rowCount())
        ? m_activeRow : 0;
        fieldsTable->selectRow(sel);
    } else {
        clearPropsUi();
    }

    refreshAutonumLocks();

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
    auto *pk = new QCheckBox(fieldsTable);
    pk->setChecked(fd.pk);
    auto *wrap = new QWidget(fieldsTable);
    auto *hl   = new QHBoxLayout(wrap);
    hl->setContentsMargins(0,0,0,0);
    hl->addStretch();
    hl->addWidget(pk);
    hl->addStretch();
    fieldsTable->setCellWidget(row, 2, wrap);

    // ⬇️ ENLACE para hacer cumplir “solo 1 PK”
    connect(pk, &QCheckBox::toggled, this, [=](bool on){
        if (m_updatingUi) return;
        Schema s = m_currentSchema;
        s[row].pk = on;
        if (on) s[row].requerido = true;

        QString err;
        if (!DataModel::instance().setSchema(m_currentTable, s, &err)) {
            QSignalBlocker b(pk);
            pk->setChecked(!on);
            if (on) { // ← solo avisar cuando intentan AGREGAR una PK extra
                QMessageBox::warning(this, tr("Clave primaria"), err);
            }
            return;
        }

        applySchemaAndRefresh(s, row);
    });

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

    // --- preservar SOLO scroll vertical ---
    const int keepV = fieldsTable->verticalScrollBar()->value();

    // Bloquea eventos de selección durante la recarga para evitar saltar a la fila 0
    {
        QSignalBlocker bSel(fieldsTable->selectionModel());
        loadTableToUi(m_currentTable);               // reconstruye grilla sin selectionChanged
        if (row >= 0 && row < fieldsTable->rowCount())
            fieldsTable->selectRow(row);             // re-selecciona la fila del usuario

        // --- restaurar scroll vertical ---
        fieldsTable->verticalScrollBar()->setValue(keepV);
    }

    // 👇 **AÑADE ESTO** (después de re-seleccionar la fila)
    m_activeRow = (row >= 0 && row < m_currentSchema.size()) ? row : -1;

    // Refresca el panel para la fila realmente seleccionada
    m_currentSchema = DataModel::instance().schema(m_currentTable);
    if (row >= 0 && row < m_currentSchema.size()) {
        loadFieldPropsToUi(m_currentSchema[row]);
        updateGeneralUiForType(m_currentSchema[row].type);
    } else {
        clearPropsUi();
    }

    refreshAutonumLocks();

    emit schemaChanged(m_currentTable, m_currentSchema);
    return true;
}



void TablesPage::connectRowEditors(int row) {
    // ----- Tipo (columna 1) -----
    auto *typeCb = qobject_cast<QComboBox*>(fieldsTable->cellWidget(row, 1));
    if (typeCb) {
        QObject::connect(typeCb, &QComboBox::currentTextChanged, this, [=](const QString &t){
            if (row < 0 || row >= m_currentSchema.size()) return;

            Schema s = m_currentSchema;

            // Tipo anterior y nuevo (normalizados)
            const QString oldNt = normType(s[row].type);
            const QString newNt = normType(t);

            // Ajusta tamaño por defecto cuando cambias entre short/long text
            if (newNt == "texto_largo") {
                // Si venimos de short text o size inválido, fija 64000
                if (oldNt == "texto" || s[row].size <= 0 || s[row].size > 64000) {
                    s[row].size = 64000;
                }
            } else if (newNt == "texto") {
                // Si venimos de long text o size inválido, fija 255
                if (oldNt == "texto_largo" || s[row].size <= 0 || s[row].size > 255) {
                    s[row].size = 255;
                }
            }
            // Cambia el tipo
            const QString oldText = s[row].type;
            s[row].type = t;

            // Refresca panel General y aplica schema
            updateGeneralUiForType(t);
            if (!applySchemaAndRefresh(s, row)) {
                // ⬅️ Si el modelo lo rechazó, REVERSA el combo y el panel
                QSignalBlocker block(typeCb);
                typeCb->setCurrentText(oldText);                          // ← vuelve a lo anterior
                updateGeneralUiForType(oldText);
                refreshAutonumLocks();
                return;
            }
            refreshAutonumLocks();

        });
    }
}


void TablesPage::onNameItemEdited(QTableWidgetItem *it) {
    if (!it || it->column() != 0) return;
    const int row = it->row();
    if (row < 0 || row >= m_currentSchema.size()) return;

    const QString newName = it->text().trimmed();
    const QString oldName = m_currentSchema[row].name;

    // Vacío: no aceptamos; vuelve a editar el mismo item
    if (newName.isEmpty()) {
        QMessageBox::warning(this, tr("Nombre inválido"),
                             tr("El nombre del campo no puede estar vacío."));
        {
            QSignalBlocker blk(fieldsTable);
            it->setText(oldName);
        }
        QTimer::singleShot(0, this, [=]{
            fieldsTable->setCurrentCell(row, 0);
            fieldsTable->editItem(fieldsTable->item(row, 0));
        });
        return;
    }

    // Duplicado (case-insensitive) en otra fila: NO guardes, fuerza a corregir
    if (nameExistsCI(m_currentSchema, newName, row)) {
        QMessageBox::warning(this, tr("Nombre duplicado"),
                             tr("Ya existe un campo llamado \"%1\".\n"
                                "Cambia el nombre para continuar.").arg(newName));
        {
            QSignalBlocker blk(fieldsTable);
            it->setText(oldName);           // revertimos visualmente
        }
        QTimer::singleShot(0, this, [=]{
            fieldsTable->setCurrentCell(row, 0);
            fieldsTable->editItem(fieldsTable->item(row, 0));  // vuelve a modo edición
        });
        return;
    }

    // OK → aplica al esquema y refresca preservando la fila
    Schema s = m_currentSchema;
    s[row].name = newName;
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
    m_activeRow = row;

    loadFieldPropsToUi(m_currentSchema[row]);
    updateGeneralUiForType(m_currentSchema[row].type);   // Ajusta visibilidad por tipo
}

void TablesPage::loadFieldPropsToUi(const FieldDef &fd) {
    QSignalBlocker b1(propFormato), b2(propDecimalPlaces),  b3(propTitulo), b7(propIndexado), b8(propRequerido);

    // NUEVO: usa la clave “limpia” y saca los decimales del sufijo |dp=X
    const QString rawFmt = fd.formato;
    propFormato->setCurrentText(baseFormatKey(rawFmt));
    propDecimalPlaces->setCurrentText(QString::number(parseDecPlaces(rawFmt)));    propAutoFormato->setCurrentText(fd.autoSubtipo);
    propAutoFormato->setCurrentText(fd.autoSubtipo.isEmpty() ? "Long Integer" : fd.autoSubtipo);
    propAutoNewValues->setCurrentText(fd.autoNewValues.isEmpty() ? "Increment" : fd.autoNewValues);

    if (normType(fd.type) == "numero") {
        const QString sz = propAutoFormato->currentText().trimmed().toLower();
        const bool isInt = sz.contains("byte") || sz.contains("entero")
                           || sz.contains("integer") || sz.contains("long");
        tuneDecimalPlacesUi(propDecimalPlaces, isInt);
    }

    // Field size (aplica a Texto corto y Texto largo)
    const QString nt = normType(fd.type);
    if (nt == "texto") {
        const int sz = (fd.size <= 0 || fd.size > 255) ? 255 : fd.size;
        propTextSize->setText(QString::number(sz));
    } else if (nt == "texto_largo") {
        const int sz = (fd.size <= 0 || fd.size > 64000) ? 64000 : fd.size;
        propTextSize->setText(QString::number(sz));
    }

    propTitulo->setText(fd.titulo);
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
        if (propFormato->lineEdit())
            propFormato->lineEdit()->setPlaceholderText("Lps / $ / €  (p.ej. #,##0.00)");


    } else if (t == "moneda") {
        propFormato->setPlaceholderText("Lps / $ / €  (p.ej. L #,##0.00)");

    } else if (t == "fecha_hora") {
        if (propFormato->lineEdit())
            propFormato->lineEdit()->setPlaceholderText("DD-MM-YY, DD/MM/YY, DD/MESTEXTO/YYYY");


    } else if (t == "booleano") {
        propFormato->setPlaceholderText("Sí/No (o Verdadero/Falso)");

    }
}

void TablesPage::pullPropsFromUi(FieldDef &fd)
{
    // Tipo normalizado del campo
    const QString nt = normType(fd.type);

    // --- DECIMALES (leer del combo y normalizar) ---
    bool ok = false;
    int rawDp = propDecimalPlaces->currentText().toInt(&ok);
    int dp = std::clamp(ok ? rawDp : 0, 0, 4);

    // ¿El tamaño/subtipo seleccionado es un entero?
    const QString sz = propAutoFormato->currentText().trimmed().toLower();
    const bool isInt = (nt == "numero") &&
                       (sz.contains("byte") || sz.contains("entero") ||
                        sz.contains("integer") || sz.contains("long"));

    if (isInt) {
        dp = 0;                // Entero: siempre 0
    } else if (nt == "numero") {
        dp = std::max(1, dp);  // Decimal/Doble: nunca 0 (mínimo 1). Default 2 lo maneja la UI.
    }

    // --- FORMATO ---
    if (nt == "numero") {
        // Número sin formato: guarda solo los decimales
        fd.formato = QStringLiteral("dp=%1").arg(dp);
    } else {
        // Moneda/Text/etc.: base + |dp=
        const QVariant key = propFormato->currentData();
        QString base = key.isValid() ? key.toString() : propFormato->currentText();
        base = base.trimmed();

        // Default de MONEDA si el usuario no tocó el combo
        if (nt == "moneda" && base.isEmpty())
            base = "LPS";

        // Fallback genérico para otros (por si quedara vacío)
        if (base.isEmpty())
            base = "Millares";

        fd.formato = base + QStringLiteral("|dp=%1").arg(dp);
    }

    // --- Otras propiedades sin cambios ---
    fd.autoSubtipo   = propAutoFormato->currentText();
    fd.autoNewValues = propAutoNewValues->currentText();
    fd.titulo        = propTitulo->text();
    fd.requerido     = propRequerido->isChecked();
    fd.indexado      = propIndexado->currentText();

    // Tamaño para texto corto/largo
    if (nt == "texto" || nt == "texto_largo") {
        ok = false;
        int v = propTextSize->text().toInt(&ok);
        if (!ok) v = (nt == "texto" ? 255 : 64000);
        const int maxSz = (nt == "texto" ? 255 : 64000);
        fd.size = std::max(1, std::min(v, maxSz));
    }

    // Si algún día agregas FieldDef.decimales, podrías guardar aquí también:
    // if (nt == "numero") fd.decimales = dp;
}



// #include <QTimer>
void TablesPage::onPropertyChanged() {
    if (m_updatingUi) return;

    const int cr  = fieldsTable->currentRow();
    const int row = (cr >= 0 ? cr : m_activeRow);
    if (row < 0 || row >= m_currentSchema.size()) return;

    // Si cambió el subtipo/tamaño del número, recalibra los decimales
    if (sender() == propAutoFormato && normType(m_currentSchema[row].type) == "numero") {
        const QString sz = propAutoFormato->currentText().trimmed().toLower();
        const bool isInt = sz.contains("byte") || sz.contains("entero")
                           || sz.contains("integer") || sz.contains("long");
        tuneDecimalPlacesUi(propDecimalPlaces, isInt);
    }

    // ¿Quién disparó?
    QObject* src = sender();
    enum class Src {None, Formato, DecPlaces, AutoFormato, ValorPred, TextSize};
    Src who = Src::None;
    if      (src == propFormato)         who = Src::Formato;
    else if (src == propDecimalPlaces)   who = Src::DecPlaces;
    else if (src == propAutoFormato)     who = Src::AutoFormato;
    else if (src == propTextSize)        who = Src::TextSize;

    // --- Construye un schema "candidato" desde la UI (sin comprometer aún el modelo) ---
    Schema s = m_currentSchema;
    pullPropsFromUi(s[row]);  // esto actualiza s[row] con lo que se ve en la UI

    // --- Regla Autonumeración: solo 1 por tabla y siempre requerido ---
    if (who == Src::Formato) {
        const QString newNorm = normType(propFormato->currentText());
        if (newNorm == "autonumeracion") {
            // ¿ya existe otro autonum en esta tabla (excluyendo la fila actual)?
            int already = -1;
            for (int i = 0; i < m_currentSchema.size(); ++i) {
                if (i == row) continue;
                if (normType(m_currentSchema[i].type) == "autonumeracion") { already = i; break; }
            }
            if (already >= 0) {
                QMessageBox::warning(this, tr("Autonumeración"),
                                     tr("Solo se permite un campo de Autonumeración por tabla."));
                // Revertir visualmente el combo y el panel de propiedades al tipo anterior
                {
                    QSignalBlocker bFmt(propFormato);
                    if (propFormato) propFormato->setCurrentText(m_currentSchema[row].type);
                }
                updateGeneralUiForType(m_currentSchema[row].type);
                if (propFormato) propFormato->setFocus();
                refreshAutonumLocks();
                return; // aborta el cambio de tipo
            }
        }
    }

    // Si el candidato quedó como autonumeración, forzar requerido = true
    if (normType(s[row].type) == "autonumeracion") {
        s[row].requerido = true;
    }

    // Casito especial previo (ya lo tenías): si solo cambian decimales en moneda, evita repoblar todo
    const QString ntPrev = normType(m_currentSchema[row].type);
    if (who == Src::DecPlaces && ntPrev == "moneda") {
        DataModel::instance().setSchema(m_currentTable, s, nullptr);
        const QString savedFmt = s[row].formato;
        const int dp = parseDecPlaces(savedFmt);
        propDecimalPlaces->blockSignals(true);
        propDecimalPlaces->setCurrentText(QString::number(dp));
        propDecimalPlaces->blockSignals(false);
        if (propDecimalPlaces) propDecimalPlaces->setFocus();
        return;
    }

    // 🔒 Bloquea reentradas durante TODO el refresh
    m_updatingUi = true;
    QScopeGuard done{[&]{ m_updatingUi = false; }};

    // (opcional) bloquea señales de combos mientras refrescas
    QSignalBlocker b1(propFormato);
    QSignalBlocker b2(propDecimalPlaces);
    QSignalBlocker b3(propAutoFormato);

    // Si el tipo NUEVO es fecha, asegúrate de dejar un formato de fecha válido
    if (normType(s[row].type) == "fecha_hora") {
        s[row].formato = baseFormatKey(s[row].formato);      // quita "|dp=.."
        if (s[row].formato.trimmed().isEmpty() ||
            s[row].formato.contains("dp="))                   // por si acaso
        {
            s[row].formato = QStringLiteral("dd/MM/yy");
        }
    }

    // Si el tipo NUEVO ya no es número/moneda, elimina el "|dp=.."
    if (normType(s[row].type) != "numero" && normType(s[row].type) != "moneda") {
        s[row].formato = baseFormatKey(s[row].formato);
    }


    // Aplica el schema candidato y refresca la UI
    if (!applySchemaAndRefresh(s, row)) {
        // ← El modelo lo rechazó: REVERSA solo si el disparador fue el Formato
        if (who == Src::Formato && propFormato) {
            QSignalBlocker bFmt(propFormato);
            propFormato->setCurrentText(m_currentSchema[row].type);
            updateGeneralUiForType(m_currentSchema[row].type);
        }
        refreshAutonumLocks();
        return;
    }
    refreshAutonumLocks();


    // Restaura el foco al control que originó el cambio
    switch (who) {
    case Src::Formato:       if (propFormato)       propFormato->setFocus();       break;
    case Src::DecPlaces:     if (propDecimalPlaces) propDecimalPlaces->setFocus(); break;
    case Src::AutoFormato:   if (propAutoFormato)   propAutoFormato->setFocus();   break;
    case Src::TextSize:      if (propTextSize)      propTextSize->setFocus();      break;
    case Src::None: default: break;
    }
}



void TablesPage::clearPropsUi() {
    QSignalBlocker b1(propFormato), b3(propTitulo),
        b7(propIndexado), b8(propRequerido);
    propFormato->setCurrentText("");
    propTitulo->clear();
    propRequerido->setChecked(false);
    propIndexado->setCurrentIndex(0);
}

/* =================== CRUD de campos =================== */
void TablesPage::onAddField() {
    if (m_currentTable.isEmpty()) return;
    Schema s = m_currentSchema;

    FieldDef fd;
    fd.name = genUniqueName(s, "NuevoCampo");   // <- ahora único (CI)
    fd.type = "Texto corto";
    fd.size = 255;

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

    auto syncRequired = [&](bool isPk, int row){
        setRowVisible(fl, propRequerido, true);
        QSignalBlocker bReq(propRequerido);
        bool req = isPk ? true
                        : ((row >= 0 && row < m_currentSchema.size())
                               ? m_currentSchema[row].requerido
                               : false);
        propRequerido->setChecked(req);
        propRequerido->setEnabled(!isPk);
    };


    // Oculta todo por defecto
    setRowVisible(fl, propFormato,     false);
    setRowVisible(fl, propAutoFormato, false);
    setRowVisible(fl, propAutoNewValues, false);   // ocúltalo por defecto
    setRowVisible(fl, propDecimalPlaces, false);

    setRowVisible(fl, propRequerido,   false);
    setRowVisible(fl, propTextSize,    false);

    // Resetea combos
    if (propFormato)       { propFormato->clear(); propFormato->clearEditText(); }
    if (propAutoFormato)   { propAutoFormato->clear(); propAutoFormato->setEnabled(true); }
    if (propAutoNewValues) propAutoNewValues->hide(); // no se usa visualmente

    // Mostrar según tipo
    if (t == "autonumeracion") {
        // Reseteo liviano
        propAutoFormato->clear();

        // ---- TAMAÑO ----
        propAutoFormato->clear();
        propAutoFormato->addItem("Long Integer");
        propAutoFormato->setCurrentIndex(0);
        propAutoFormato->setEnabled(false);
        setRowVisible(fl, propAutoFormato, true);

        // ---- FORMATO ---- (no se usa en autonum)
        setRowVisible(fl, propFormato, false);
        if (propFormato) { propFormato->clear(); propFormato->clearEditText(); }

        // ---- NEW VALUES ---- (solo "Increment" y bloqueado)
        setRowVisible(fl, propAutoNewValues, true);
        propAutoNewValues->blockSignals(true);
        propAutoNewValues->clear();
        propAutoNewValues->addItem("Increment");
        propAutoNewValues->setCurrentIndex(0);
        propAutoNewValues->setEnabled(false);        // ← así NO se despliega
        propAutoNewValues->setFocusPolicy(Qt::NoFocus);
        propAutoNewValues->blockSignals(false);

        // ---- REQUERIDO: Autonumeración SIEMPRE requerido y bloqueado ----
        setRowVisible(fl, propRequerido, true);
        if (propRequerido) {
            QSignalBlocker bReq(propRequerido);
            propRequerido->setChecked(true);
            propRequerido->setEnabled(false);
            propRequerido->setToolTip(tr("Los campos Autonumeración siempre son Requeridos."));
        }


        // nada de valor predeterminado / field size
        setRowVisible(fl, propTextSize,  false);

        return;
    }

    if (normType(type) == "numero") {
        // ---- TAMAÑO (subtipo numérico) ----
        setRowVisible(fl, propAutoFormato, true);
        propAutoFormato->blockSignals(true);
        propAutoFormato->clear();
        propAutoFormato->addItems({"Byte","Entero","Decimal","Doble"});

        int r = fieldsTable ? fieldsTable->currentRow() : -1;
        QString saved = (r >= 0 && r < m_currentSchema.size() && !m_currentSchema[r].autoSubtipo.isEmpty())
                            ? m_currentSchema[r].autoSubtipo
                            : QStringLiteral("Entero");
        const QMap<QString, QString> alias = {
            {"Integer","Entero"}, {"Long Integer","Entero"},
            {"Single","Decimal"}, {"Double","Doble"}, {"Decimal","Decimal"}
        };
        if (alias.contains(saved)) saved = alias.value(saved);
        if (propAutoFormato->findText(saved) < 0) saved = "Entero";
        propAutoFormato->setCurrentText(saved);
        propAutoFormato->blockSignals(false);

        // ---- FORMATO: oculto para Número (quitamos formato) ----
        setRowVisible(fl, propFormato, false);

        // ---- DECIMAL PLACES ----
        setRowVisible(fl, propDecimalPlaces, true);

        const QString savedFmt = (r >= 0 && r < m_currentSchema.size())
                                     ? m_currentSchema[r].formato.trimmed()
                                     : QString();
        const int savedDp = parseDecPlaces(savedFmt); // 0..4

        const QString sz = propAutoFormato->currentText().trimmed().toLower();
        const bool isInt = sz.contains("byte") || sz.contains("entero")
                           || sz.contains("integer") || sz.contains("long");

        // Usa el helper ya definido arriba del archivo
        tuneDecimalPlacesUi(propDecimalPlaces, isInt, savedDp);

        setRowVisible(fl, propTextSize,  false);
        syncRequired(isPk, row);
        return;
    }



    if (t == "texto") {
        propFormato->setEditable(true);
        // Solo aplica tamaño de texto (Field size) + valor predeterminado
        setRowVisible(fl, propTextSize,    true);
        syncRequired(isPk, row);

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

    if (t == "texto_largo") {
        propFormato->setEditable(true);
        // Igual que short text: mostramos Field size + valor predeterminado
        setRowVisible(fl, propTextSize,  true);
        syncRequired(isPk, row);

        // Ajusta el label
        if (auto *lbl = qobject_cast<QLabel*>(fl->labelForField(propTextSize)))
            lbl->setText("Field size:");

        // ← AQUÍ (validador + placeholder)
        propTextSize->setValidator(new QIntValidator(1, 64000, propTextSize));
        propTextSize->setPlaceholderText(QStringLiteral("1..64000"));

        // PONER EL NÚMERO EN EL QLINEEDIT
        int idx = fieldsTable ? fieldsTable->currentRow() : -1;
        if (idx >= 0 && idx < m_currentSchema.size()) {
            int sz = m_currentSchema[idx].size > 0 ? m_currentSchema[idx].size : 64000;
            if (sz < 1)      sz = 1;
            if (sz > 64000)  sz = 64000;
            propTextSize->setText(QString::number(sz));
        } else {
            propTextSize->setText(QStringLiteral("64000"));
        }
        return;
    }


    // ---- Fecha ----
    if (t == "fecha_hora") {
        setRowVisible(fl, propFormato, true);

        // --- FECHA: poblar combo con 3 formatos fijos ---
        propFormato->blockSignals(true);
        propFormato->clear();
        propFormato->setEditable(false);

        propFormato->addItem("DD-MM-YY",          "dd-MM-yy");
        propFormato->addItem("DD/MM/YY",          "dd/MM/yy");      // ← default
        propFormato->addItem("DD/MESTEXTO/YYYY",  "dd/MMMM/yyyy");

        int r = fieldsTable ? fieldsTable->currentRow() : -1;
        QString saved = (r >= 0 && r < m_currentSchema.size())
                            ? baseFormatKey(m_currentSchema[r].formato)
                            : QString();
        if (saved.isEmpty()) {
            saved = "dd/MM/yy";                      // default
            if (r >= 0 && r < m_currentSchema.size())
                m_currentSchema[r].formato = saved;  // persiste en schema
        }
        int idx = propFormato->findData(saved);
        propFormato->setCurrentIndex(idx < 0 ? propFormato->findData("dd/MM/yy") : idx);
        propFormato->blockSignals(false);


        // Oculta lo que no aplica en fecha
        setRowVisible(fl, propDecimalPlaces, false);
        setRowVisible(fl, propTextSize,      false);
        setRowVisible(fl, propAutoFormato,   false);
        setRowVisible(fl, propAutoNewValues, false);

        // Requerido (respeta PK)
        syncRequired(isPk, row);
        return;
    }


    // ---- Sí/No (booleano) ----
    if (t == "booleano") {
        setRowVisible(fl, propFormato, true);
        propFormato->blockSignals(true);
        propFormato->clear();

        // ÚNICA opción: “Sí/No” (clave en userData)
        const QString key = QStringLiteral("Sí/No");
        propFormato->addItem(QStringLiteral("Sí/No"), key);
        propFormato->setCurrentIndex(0);

        // No editable y sin interacción (solo informativo)
        propFormato->setEditable(false);
        propFormato->setEnabled(false);
        propFormato->setFocusPolicy(Qt::NoFocus);

        // Si había algo guardado distinto (True/False, Yes/No, On/Off), lo mostramos como “Sí/No”
        int r = fieldsTable ? fieldsTable->currentRow() : -1;
        if (r >= 0 && r < m_currentSchema.size()) {
            m_currentSchema[r].formato = key; // normaliza a "Sí/No"
        }

        // Oculta lo que no aplica
        setRowVisible(fl, propDecimalPlaces, false);
        setRowVisible(fl, propTextSize,      false);
        setRowVisible(fl, propAutoFormato,   false);
        setRowVisible(fl, propAutoNewValues, false);

        // Requerido (si es PK queda bloqueado)
        syncRequired(isPk, row);
        propFormato->blockSignals(false);
        return;
    }


    if (t == "moneda") {
        setRowVisible(fl, propAutoFormato,   false);
        setRowVisible(fl, propAutoNewValues, false);
        setRowVisible(fl, propTextSize,      false);

        // Formatos: LPS, USD, EUR, Millares
        setRowVisible(fl, propFormato, true);
        propFormato->blockSignals(true);
        propFormato->clear();

        QStringList keys = {"LPS","USD","EUR","Millares"};
        int pad = 0; for (const auto& k : keys) pad = qMax(pad, k.size());
        auto addFmt = [&](const QString& key, const QString& sample){
            const QString left = key.leftJustified(pad + 2, QChar(' '));
            propFormato->addItem(left + sample, key);   // userData = clave limpia
        };
        addFmt("LPS",      "L 3,456.79");
        addFmt("USD",      "$3,456.79");
        addFmt("EUR",      "€3,456.79");
        addFmt("Millares", "M 3,456.79");

        // Seleccionar lo guardado; si no hay, usar LPS
        int r = fieldsTable ? fieldsTable->currentRow() : -1;
        const QString savedFmt = (r >= 0 && r < m_currentSchema.size())
                                     ? m_currentSchema[r].formato.trimmed()
                                     : QString();
        const QString fmt = baseFormatKey(savedFmt).isEmpty()
                                ? QStringLiteral("LPS")
                                : baseFormatKey(savedFmt);

        int ix = 0;
        for (int i = 0; i < propFormato->count(); ++i)
            if (propFormato->itemData(i).toString() == fmt) { ix = i; break; }
        propFormato->setCurrentIndex(ix);

        propFormato->blockSignals(false);

        setRowVisible(fl, propDecimalPlaces, true);
        propDecimalPlaces->blockSignals(true);

        // ← Asegura el menú 0..4 SOLO aquí para moneda
        propDecimalPlaces->clear();
        propDecimalPlaces->addItems({"0","1","2","3","4"});

        const int dp = std::clamp(parseDecPlaces(savedFmt), 0, 4);
        propDecimalPlaces->setCurrentText(QString::number(dp));
        propDecimalPlaces->setEnabled(true);

        // hacer editable el combo de decimales solo para Moneda
        propDecimalPlaces->setEditable(true);
        propDecimalPlaces->setValidator(new QIntValidator(0, 4, propDecimalPlaces->lineEdit()));
        propDecimalPlaces->lineEdit()->setPlaceholderText(tr("0–4"));
        propDecimalPlaces->setToolTip(tr("Decimales para moneda (0–4)"));
        propDecimalPlaces->blockSignals(false);



        syncRequired(isPk, row);
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
        propTitulo, propIndexado,
        propTextSize, propDecimalPlaces
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




