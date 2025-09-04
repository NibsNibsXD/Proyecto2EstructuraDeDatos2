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
#include <algorithm>        // std::sort, std::clamp

// ---- helper de tipo (texto -> etiqueta estable) ----
static inline QString normType(const QString& t) {
    const QString s = t.trimmed().toLower();
    if (s.startsWith(u"auto")) return "autonumeracion";
    if (s.startsWith(u"número") || s.startsWith(u"numero")) return "numero";
    if (s.startsWith(u"fecha")) return "fecha_hora";
    if (s.startsWith(u"moneda")) return "moneda";
    return "texto";
}

RecordsPage::RecordsPage(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::RecordsPage)
{
    ui->setupUi(this);

    // ---- Conexiones de encabezado ----
    connect(ui->cbTabla,              QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordsPage::onTablaChanged);
    connect(ui->leBuscar,             &QLineEdit::textChanged,
            this, &RecordsPage::onBuscarChanged);
    connect(ui->btnLimpiarBusqueda,   &QToolButton::clicked,
            this, &RecordsPage::onLimpiarBusqueda);

    connect(ui->btnInsertar,          &QToolButton::clicked, this, &RecordsPage::onInsertar);
    connect(ui->btnEditar,            &QToolButton::clicked, this, &RecordsPage::onEditar);
    connect(ui->btnEliminar,          &QToolButton::clicked, this, &RecordsPage::onEliminar);
    connect(ui->btnGuardar,           &QToolButton::clicked, this, &RecordsPage::onGuardar);
    connect(ui->btnCancelar,          &QToolButton::clicked, this, &RecordsPage::onCancelar);

    // ---- Tabla ----
    connect(ui->twRegistros,          &QTableWidget::itemSelectionChanged,
            this, &RecordsPage::onSelectionChanged);
    connect(ui->twRegistros,          &QTableWidget::itemDoubleClicked,
            this, &RecordsPage::onItemDoubleClicked);

    // ---- Editor (maqueta) ----
    connect(ui->btnLimpiarForm,       &QToolButton::clicked, this, &RecordsPage::onLimpiarFormulario);
    connect(ui->btnGenerarDummy,      &QToolButton::clicked, this, &RecordsPage::onGenerarDummyFila);

    // ---- Paginación (etiqueta) ----
    connect(ui->btnPrimero,           &QToolButton::clicked, this, &RecordsPage::onPrimero);
    connect(ui->btnAnterior,          &QToolButton::clicked, this, &RecordsPage::onAnterior);
    connect(ui->btnSiguiente,         &QToolButton::clicked, this, &RecordsPage::onSiguiente);
    connect(ui->btnUltimo,            &QToolButton::clicked, this, &RecordsPage::onUltimo);

    // Config base de la tabla
    ui->twRegistros->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->twRegistros->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->twRegistros->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->twRegistros->setAlternatingRowColors(true);
    ui->twRegistros->setSortingEnabled(true);
    ui->twRegistros->verticalHeader()->setVisible(false);
    ui->twRegistros->horizontalHeader()->setStretchLastSection(true);

    // Demo por defecto si no hay tabla (se mostrará hasta que Shell/Tablas seleccionen)
    ui->cbTabla->clear();
    ui->cbTabla->addItems(QStringList() << "Estudiantes" << "Cursos" << "Inscripciones");
    construirColumnasDemo();
    cargarDatosDemo();

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
    // Desuscribir señal anterior de filas
    if (m_rowsConn) {
        disconnect(m_rowsConn);
        m_rowsConn = QMetaObject::Connection();
    }

    m_tableName = name;
    m_schema    = defs;

    // Refleja el nombre en el combo sin disparar onTablaChanged
    {
        QSignalBlocker block(ui->cbTabla);
        int idx = ui->cbTabla->findText(name);
        if (idx < 0) {
            ui->cbTabla->addItem(name);
            idx = ui->cbTabla->count() - 1;
        }
        ui->cbTabla->setCurrentIndex(idx);
    }

    ui->lblFormTitle->setText(QStringLiteral("Editor — %1").arg(name.isEmpty() ? "sin selección" : name));

    applyDefs(defs);
    reloadRows();

    // Suscribirse a cambios de filas del modelo
    m_rowsConn = connect(&DataModel::instance(), &DataModel::rowsChanged, this,
                         [this](const QString& t){
                             if (t == m_tableName) reloadRows();
                         });

    setMode(Mode::Idle);
    updateStatusLabels();
    updateNavState();
}

/* =================== Construcción de columnas desde el esquema =================== */

void RecordsPage::applyDefs(const Schema& defs)
{
    ui->twRegistros->clear();
    ui->twRegistros->setRowCount(0);

    if (defs.isEmpty()) {
        ui->twRegistros->setColumnCount(0);
        return;
    }

    ui->twRegistros->setColumnCount(defs.size());
    QStringList headers;
    headers.reserve(defs.size());
    for (const auto& f : defs) headers << f.name;
    ui->twRegistros->setHorizontalHeaderLabels(headers);
}

QString RecordsPage::formatCell(const FieldDef& fd, const QVariant& v) const
{
    if (!v.isValid() || v.isNull()) return QString();
    const QString t = normType(fd.type);
    if (t == "fecha_hora") return v.toDate().toString("yyyy-MM-dd");
    if (t == "moneda")     return QString::number(v.toDouble(), 'f', 2);
    // Numero / Auto -> as string
    return v.toString();
}

void RecordsPage::reloadRows()
{
    if (m_tableName.isEmpty() || m_schema.isEmpty()) return;

    const auto& vec = DataModel::instance().rows(m_tableName);

    // Intentar preservar selección
    int prevSel = -1;
    if (auto *sm = ui->twRegistros->selectionModel()) {
        const auto rows = sm->selectedRows();
        if (!rows.isEmpty()) prevSel = rows.first().row();
    }

    QSignalBlocker block(ui->twRegistros);
    ui->twRegistros->setRowCount(0);

    for (int i = 0; i < vec.size(); ++i) {
        const Record& r = vec[i];
        const int row = ui->twRegistros->rowCount();
        ui->twRegistros->insertRow(row);
        for (int c = 0; c < m_schema.size(); ++c) {
            const FieldDef& fd = m_schema[c];
            const QVariant  vv = (c < r.size() ? r[c] : QVariant());
            ui->twRegistros->setItem(row, c, new QTableWidgetItem(formatCell(fd, vv)));
        }
    }

    // Restaurar selección (o seleccionar la primera si nada)
    if (ui->twRegistros->rowCount() > 0) {
        int sel = std::clamp(prevSel, 0, ui->twRegistros->rowCount() - 1);
        ui->twRegistros->selectRow(sel);
    }
    updateStatusLabels();
    updateNavState();
}

/* =================== Helpers de estado/UI =================== */

void RecordsPage::setMode(Mode m)
{
    m_mode = m;
    // Guardar/Cancelar quedan deshabilitados en este flujo
    updateHeaderButtons();
}

void RecordsPage::updateHeaderButtons()
{
    const bool haveSel = !ui->twRegistros->selectedItems().isEmpty();
    ui->btnInsertar->setEnabled(true);
    ui->btnEditar->setEnabled(haveSel);
    ui->btnEliminar->setEnabled(haveSel);
    ui->btnGuardar->setEnabled(false);
    ui->btnCancelar->setEnabled(false);
}

void RecordsPage::updateStatusLabels()
{
    int total = ui->twRegistros->rowCount();
    int visibles = 0;
    for (int r = 0; r < total; ++r)
        if (!ui->twRegistros->isRowHidden(r)) ++visibles;

    ui->lblTotal->setText(QStringLiteral("Mostrando %1 de %2").arg(visibles).arg(total));
    ui->lblPagina->setText(QStringLiteral("%1 / %2").arg(m_currentPage).arg(1)); // mock
}

/* =================== Construcción demo (legacy) =================== */

void RecordsPage::construirColumnasDemo()
{
    ui->twRegistros->clear();
    ui->twRegistros->setColumnCount(6);
    ui->twRegistros->setHorizontalHeaderLabels(
        QStringList() << "ID" << "Nombre" << "Correo" << "Teléfono" << "Fecha" << "Activo");
}

void RecordsPage::cargarDatosDemo()
{
    ui->twRegistros->setRowCount(0);
    for (int i = 0; i < 30; ++i) {
        const int row = ui->twRegistros->rowCount();
        ui->twRegistros->insertRow(row);

        auto* c0 = new QTableWidgetItem(QString::number(1000 + i));
        auto* c1 = new QTableWidgetItem(QStringLiteral("Nombre %1").arg(i + 1));
        auto* c2 = new QTableWidgetItem(QStringLiteral("user%1@email.com").arg(i + 1));
        auto* c3 = new QTableWidgetItem(QStringLiteral("9999-00%1").arg(i % 10));
        auto* c4 = new QTableWidgetItem(QDate::currentDate().addDays(-i).toString("yyyy-MM-dd"));
        auto* c5 = new QTableWidgetItem((i % 3 == 0) ? "No" : "Sí");

        ui->twRegistros->setItem(row, 0, c0);
        ui->twRegistros->setItem(row, 1, c1);
        ui->twRegistros->setItem(row, 2, c2);
        ui->twRegistros->setItem(row, 3, c3);
        ui->twRegistros->setItem(row, 4, c4);
        ui->twRegistros->setItem(row, 5, c5);
    }
}

/* =================== Acciones de encabezado =================== */

void RecordsPage::onTablaChanged(int /*index*/)
{
    // En sandbox: reconstruimos demo si no hay selección de Shell
    if (m_tableName.isEmpty()) {
        construirColumnasDemo();
        cargarDatosDemo();
        ui->leBuscar->clear();
        setMode(Mode::Idle);
        updateStatusLabels();
        updateNavState();
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

/* ============ CRUD real con DataModel ============ */

bool RecordsPage::editRecordDialog(const QString& title, const Schema& s, Record& r, bool isInsert, QString* errMsg)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    auto *vl  = new QVBoxLayout(&dlg);
    auto *fl  = new QFormLayout;
    vl->addLayout(fl);

    // Asegurar tamaño de r
    if (r.size() < s.size()) r.resize(s.size());

    // Edits por columna
    QVector<QWidget*> editors; editors.reserve(s.size());

    for (int i = 0; i < s.size(); ++i) {
        const FieldDef& fd = s[i];
        const QString t = normType(fd.type);
        QWidget* w = nullptr;

        if (t == "autonumeracion") {
            // Solo lectura (lo asigna el modelo si está vacío)
            auto *le = new QLineEdit;
            le->setReadOnly(true);
            le->setPlaceholderText("[auto]");
            if (r[i].isValid() && !r[i].isNull()) le->setText(r[i].toString());
            w = le;
        } else if (t == "numero") {
            auto *le = new QLineEdit;
            le->setValidator(new QIntValidator(le));
            if (r[i].isValid() && !r[i].isNull()) le->setText(QString::number(r[i].toLongLong()));
            w = le;
        } else if (t == "moneda") {
            auto *ds = new QDoubleSpinBox;
            ds->setDecimals(2);
            ds->setRange(-1e12, 1e12);
            if (r[i].isValid() && !r[i].isNull()) ds->setValue(r[i].toDouble());
            w = ds;
        } else if (t == "fecha_hora") {
            auto *de = new QDateEdit;
            de->setCalendarPopup(true);
            de->setDisplayFormat("yyyy-MM-dd");
            if (r[i].canConvert<QDate>() && r[i].toDate().isValid())
                de->setDate(r[i].toDate());
            else
                de->setDate(QDate::currentDate());
            w = de;
        } else { // texto
            auto *le = new QLineEdit;
            if (fd.size > 0) le->setMaxLength(fd.size);
            if (r[i].isValid() && !r[i].isNull()) le->setText(r[i].toString());
            else if (isInsert && !fd.valorPredeterminado.isEmpty()) le->setText(fd.valorPredeterminado);
            w = le;
        }

        editors.push_back(w);
        fl->addRow(fd.name + ":", w);
    }

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    vl->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    while (true) {
        if (dlg.exec() != QDialog::Accepted) return false;

        // Volcar valores
        for (int i = 0; i < s.size(); ++i) {
            const FieldDef& fd = s[i];
            const QString t = normType(fd.type);
            QWidget* w = editors[i];

            if (t == "autonumeracion") {
                auto *le = qobject_cast<QLineEdit*>(w);
                const QString txt = le->text().trimmed();
                r[i] = txt.isEmpty() ? QVariant() : QVariant(txt.toLongLong());
            } else if (t == "numero") {
                auto *le = qobject_cast<QLineEdit*>(w);
                r[i] = le->text().trimmed().isEmpty() ? QVariant() : QVariant(le->text().toLongLong());
            } else if (t == "moneda") {
                auto *ds = qobject_cast<QDoubleSpinBox*>(w);
                r[i] = QVariant(ds->value());
            } else if (t == "fecha_hora") {
                auto *de = qobject_cast<QDateEdit*>(w);
                r[i] = QVariant(de->date());
            } else {
                auto *le = qobject_cast<QLineEdit*>(w);
                r[i] = QVariant(le->text());
            }
        }

        // Validación del modelo (convierte tipos)
        Record tmp = r;
        QString vErr;
        if (!DataModel::instance().validate(s, tmp, &vErr)) {
            if (errMsg) *errMsg = vErr;
            QMessageBox::warning(this, tr("Validación"), vErr);
            continue; // permite corregir
        }
        // Si ok, dejamos r convertido
        r = tmp;
        return true;
    }
}

void RecordsPage::onInsertar()
{
    if (m_tableName.isEmpty() || m_schema.isEmpty()) return;

    Record r; // vacío -> el modelo hará autonum si corresponde
    QString err;
    if (!editRecordDialog(tr("Insertar en %1").arg(m_tableName), m_schema, r, /*isInsert*/true, &err))
        return;

    if (!DataModel::instance().insertRow(m_tableName, r, &err)) {
        QMessageBox::warning(this, tr("Insertar"), err);
        return;
    }

    emit recordInserted(m_tableName);
    reloadRows();
    // Seleccionar última fila
    if (ui->twRegistros->rowCount() > 0)
        ui->twRegistros->selectRow(ui->twRegistros->rowCount() - 1);
    updateNavState();
}

void RecordsPage::onEditar()
{
    if (m_tableName.isEmpty() || m_schema.isEmpty()) return;

    const auto sel = ui->twRegistros->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    const int row = sel.first().row();

    const auto& vec = DataModel::instance().rows(m_tableName);
    if (row < 0 || row >= vec.size()) return;
    Record r = vec[row];

    QString err;
    if (!editRecordDialog(tr("Editar fila %1").arg(row+1), m_schema, r, /*isInsert*/false, &err))
        return;

    if (!DataModel::instance().updateRow(m_tableName, row, r, &err)) {
        QMessageBox::warning(this, tr("Editar"), err);
        return;
    }

    emit recordUpdated(m_tableName, row);
    reloadRows();
    if (row < ui->twRegistros->rowCount()) ui->twRegistros->selectRow(row);
    updateNavState();
}

void RecordsPage::onEliminar()
{
    if (m_tableName.isEmpty() || m_schema.isEmpty()) return;

    const auto selRows = ui->twRegistros->selectionModel()->selectedRows();
    if (selRows.isEmpty()) return;

    if (QMessageBox::question(this, tr("Eliminar"),
                              tr("¿Eliminar %1 registro(s)?").arg(selRows.size())) != QMessageBox::Yes) return;

    QList<int> rows;
    rows.reserve(selRows.size());
    for (const auto& mi : selRows) rows << mi.row();
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    QString err;
    if (!DataModel::instance().removeRows(m_tableName, rows, &err)) {
        QMessageBox::warning(this, tr("Eliminar"), err);
        return;
    }

    emit recordDeleted(m_tableName, rows);
    reloadRows();
    updateNavState();
}

void RecordsPage::onGuardar()  { /* no-op en este flujo */ }
void RecordsPage::onCancelar() { /* no-op en este flujo */ }

/* =================== Tabla / selección / doble clic =================== */

void RecordsPage::onSelectionChanged()
{
    const auto selRows = ui->twRegistros->selectionModel()->selectedRows();
    if (selRows.size() == 1) {
        ui->lblFormTitle->setText(QStringLiteral("Editor — (1 seleccionado)"));
    } else if (selRows.size() > 1) {
        ui->lblFormTitle->setText(QStringLiteral("Editor — (%1 seleccionados)").arg(selRows.size()));
    } else {
        ui->lblFormTitle->setText(QStringLiteral("Editor — (sin selección)"));
    }
    updateHeaderButtons();
    updateNavState();
}

void RecordsPage::onItemDoubleClicked()
{
    onEditar();
}

/* =================== Editor (maqueta legacy) =================== */

void RecordsPage::onLimpiarFormulario()
{
    limpiarFormulario();
}

void RecordsPage::onGenerarDummyFila()
{
    ui->leId->setText(QString::number(1000 + ui->twRegistros->rowCount()));
    ui->leNombre->setText(QStringLiteral("Nuevo Usuario"));
    ui->leCorreo->setText(QStringLiteral("nuevo@email.com"));
    ui->leTelefono->setText(QStringLiteral("9999-0000"));
    ui->deFecha->setDate(QDate::currentDate());
    ui->chkActivo->setChecked(true);
    ui->dsbSaldo->setValue(0.0);
    ui->pteNotas->setPlainText(QString());
    ui->leExtra1->clear();
    ui->leExtra2->clear();
    ui->leExtra3->clear();
}

/* =================== Paginación (mock) =================== */

void RecordsPage::onPrimero()  { m_currentPage = 1; updateStatusLabels(); }
void RecordsPage::onAnterior() { m_currentPage = qMax(1, m_currentPage - 1); updateStatusLabels(); }
void RecordsPage::onSiguiente(){ m_currentPage = m_currentPage + 1; updateStatusLabels(); }
void RecordsPage::onUltimo()   { m_currentPage = m_currentPage + 1; updateStatusLabels(); }

/* =================== Utilidades de formulario/tabla (legacy) =================== */

void RecordsPage::limpiarFormulario()
{
    ui->leId->clear();
    ui->leNombre->clear();
    ui->leCorreo->clear();
    ui->leTelefono->clear();
    ui->deFecha->setDate(QDate::currentDate());
    ui->chkActivo->setChecked(false);
    ui->dsbSaldo->setValue(0.0);
    ui->pteNotas->clear();
    ui->leExtra1->clear();
    ui->leExtra2->clear();
    ui->leExtra3->clear();
}

void RecordsPage::cargarFormularioDesdeFila(int row)
{
    Q_UNUSED(row)
    // Mantener vacío (legacy demo); el CRUD real usa diálogo dinámico
}

void RecordsPage::escribirFormularioEnFila(int row)
{
    Q_UNUSED(row)
    // Mantener vacío (legacy demo)
}

int RecordsPage::agregarFilaDesdeFormulario()
{
    return -1; // no usado
}

/* ----------------- Búsqueda ----------------- */

bool RecordsPage::filaCoincideBusqueda(int row, const QString& term) const
{
    if (term.isEmpty()) return true;
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    for (int c = 0; c < ui->twRegistros->columnCount(); ++c) {
        if (auto* it = ui->twRegistros->item(row, c)) {
            if (it->text().contains(term, cs)) return true;
        }
    }
    return false;
}

void RecordsPage::aplicarFiltroBusqueda(const QString& term)
{
    for (int r = 0; r < ui->twRegistros->rowCount(); ++r) {
        const bool show = filaCoincideBusqueda(r, term);
        ui->twRegistros->setRowHidden(r, !show);
    }
    updateNavState();
}

/* ----------------- Navegación visible ----------------- */

QList<int> RecordsPage::visibleRows() const
{
    QList<int> v;
    const int n = ui->twRegistros->rowCount();
    v.reserve(n);
    for (int r = 0; r < n; ++r)
        if (!ui->twRegistros->isRowHidden(r)) v.push_back(r);
    return v;
}

int RecordsPage::selectedVisibleIndex() const
{
    const auto vis = visibleRows();
    const auto sel = ui->twRegistros->selectionModel()
                         ? ui->twRegistros->selectionModel()->selectedRows()
                         : QModelIndexList{};
    if (sel.isEmpty()) return -1;
    const int row = sel.first().row();
    for (int i = 0; i < vis.size(); ++i)
        if (vis[i] == row) return i;
    return -1; // seleccionado está oculto o no hay
}

void RecordsPage::selectVisibleByIndex(int visIndex)
{
    const auto vis = visibleRows();
    if (vis.isEmpty()) return;
    // Qt6: QList::size() devuelve qsizetype → cast a int para std::clamp
    const int ix = std::clamp(visIndex, 0, int(vis.size()) - 1);
    const int row = vis[ix];
    ui->twRegistros->clearSelection();
    ui->twRegistros->selectRow(row);
    ui->twRegistros->scrollToItem(ui->twRegistros->item(row, 0),
                                  QAbstractItemView::PositionAtCenter);
    updateNavState();
}

void RecordsPage::updateNavState()
{
    // Obtener filas visibles y la selección actual
    const QList<int> visibles = visibleRows();
    int totalVisibles = visibles.size();

    // Índice seleccionado dentro de visibles (0-based)
    int curIndex = selectedVisibleIndex();

    // Convertir a 1-based (0 si no hay selección)
    int cur = (curIndex >= 0 ? curIndex + 1 : 0);

    // Calcular si se puede navegar
    bool canPrev = (cur > 1);
    bool canNext = (cur > 0 && cur < totalVisibles);

    // Emitir la señal hacia ShellWindow
    emit navState(cur, totalVisibles, canPrev, canNext);
}


/* =================== Navegación desde ShellWindow =================== */

void RecordsPage::navFirst()
{
    const QList<int> vis = visibleRows();
    if (!vis.isEmpty())
        selectVisibleByIndex(0);
}

void RecordsPage::navPrev()
{
    int cur = selectedVisibleIndex();
    if (cur > 0)
        selectVisibleByIndex(cur - 1);
}

void RecordsPage::navNext()
{
    int cur = selectedVisibleIndex();
    const QList<int> vis = visibleRows();
    if (cur >= 0 && cur < vis.size() - 1)
        selectVisibleByIndex(cur + 1);
}

void RecordsPage::navLast()
{
    const QList<int> vis = visibleRows();
    if (!vis.isEmpty())
        selectVisibleByIndex(vis.size() - 1);
}



/* =================== Ordenación desde Ribbon =================== */

int RecordsPage::currentSortColumn() const {
    // preguntar directamente al header qué columna está activa
    int col = ui->twRegistros->horizontalHeader()->sortIndicatorSection();
    if (col >= 0)
        return col;

    // si nunca se ha ordenado, por defecto la primera
    return 0;
}


void RecordsPage::sortAscending() {
    int col = currentSortColumn();
    ui->twRegistros->sortByColumn(col, Qt::AscendingOrder);
}

void RecordsPage::sortDescending() {
    int col = currentSortColumn();
    ui->twRegistros->sortByColumn(col, Qt::DescendingOrder);
}

void RecordsPage::clearSorting() {
    ui->twRegistros->setSortingEnabled(false);
    ui->twRegistros->setSortingEnabled(true);
    ui->twRegistros->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);

    reloadRows(); // vuelve a cargar en el orden natural de DataModel
}
