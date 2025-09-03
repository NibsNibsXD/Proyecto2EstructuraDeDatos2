#include "recordspage.h"
#include "ui_recordspage.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QDate>
#include <QTableWidgetItem>
#include <QSignalBlocker>
#include <algorithm>        // std::sort

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

    // ---- Editor ----
    connect(ui->btnLimpiarForm,       &QToolButton::clicked, this, &RecordsPage::onLimpiarFormulario);
    connect(ui->btnGenerarDummy,      &QToolButton::clicked, this, &RecordsPage::onGenerarDummyFila);

    // ---- Paginación (etiqueta) ----
    connect(ui->btnPrimero,           &QToolButton::clicked, this, &RecordsPage::onPrimero);
    connect(ui->btnAnterior,          &QToolButton::clicked, this, &RecordsPage::onAnterior);
    connect(ui->btnSiguiente,         &QToolButton::clicked, this, &RecordsPage::onSiguiente);
    connect(ui->btnUltimo,            &QToolButton::clicked, this, &RecordsPage::onUltimo);

    // Tabla (apariencia básica; lo fino via stylesheet)
    ui->twRegistros->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->twRegistros->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->twRegistros->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->twRegistros->setAlternatingRowColors(true);
    ui->twRegistros->setSortingEnabled(true);
    ui->twRegistros->verticalHeader()->setVisible(false);
    ui->twRegistros->horizontalHeader()->setStretchLastSection(true);

    // Conexión al modelo: refrescar si cambian filas de la tabla actual
    connect(&DataModel::instance(), &DataModel::rowsChanged, this, [this](const QString& t){
        if (t == m_tableName) {
            reloadRowsFromModel();
        }
    });

    // Inicio sandbox (se poblará desde Shell al seleccionar tabla)
    ui->cbTabla->clear();
    setMode(Mode::Idle);
    updateStatusLabels();
}

RecordsPage::~RecordsPage()
{
    delete ui;
}

/* =================== Integración con TablesPage/Shell =================== */

void RecordsPage::setTableFromFieldDefs(const QString& name, const Schema& defs)
{
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
    setMode(Mode::Idle);
    updateStatusLabels();
}

// Reconstruye columnas y rellena desde DataModel
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

    reloadRowsFromModel();
}

void RecordsPage::reloadRowsFromModel()
{
    ui->twRegistros->setRowCount(0);
    if (m_tableName.isEmpty() || m_schema.isEmpty()) { updateStatusLabels(); return; }

    const auto& rows = DataModel::instance().rows(m_tableName);

    auto cellText = [](const QVariant& v)->QString{
        if (!v.isValid() || v.isNull()) return QString();
        if (v.canConvert<QDate>()) return v.toDate().toString("yyyy-MM-dd");
        if (v.typeId() == QMetaType::Double) return QString::number(v.toDouble());
        return v.toString();
    };

    for (int i = 0; i < rows.size(); ++i) {
        ui->twRegistros->insertRow(i);
        const Record& r = rows[i];
        for (int c = 0; c < m_schema.size(); ++c) {
            const QVariant val = (c < r.size()) ? r[c] : QVariant();
            ui->twRegistros->setItem(i, c, new QTableWidgetItem(cellText(val)));
        }
    }

    aplicarFiltroBusqueda(ui->leBuscar->text());
    updateStatusLabels();
}

/* =================== Helpers de estado/UI =================== */

void RecordsPage::setMode(Mode m)
{
    m_mode = m;
    switch (m_mode) {
    case Mode::Idle:
        ui->twRegistros->setEnabled(true);
        ui->lblFormTitle->setText(QStringLiteral("Editor — (sin selección)"));
        break;
    case Mode::Insert:
        ui->twRegistros->clearSelection();
        ui->twRegistros->setEnabled(false);
        ui->lblFormTitle->setText(QStringLiteral("Editor — Insertar"));
        break;
    case Mode::Edit:
        ui->twRegistros->setEnabled(false);
        ui->lblFormTitle->setText(QStringLiteral("Editor — Editar"));
        break;
    }
    updateHeaderButtons();
}

void RecordsPage::updateHeaderButtons()
{
    const bool haveSel = !ui->twRegistros->selectionModel()->selectedRows().isEmpty();
    const bool editing = (m_mode != Mode::Idle);

    ui->btnInsertar->setEnabled(!editing && !m_schema.isEmpty());
    ui->btnEditar->setEnabled(!editing && haveSel);
    ui->btnEliminar->setEnabled(!editing && haveSel);
    ui->btnGuardar->setEnabled(editing);
    ui->btnCancelar->setEnabled(editing);
}

void RecordsPage::updateStatusLabels()
{
    int total = ui->twRegistros->rowCount();
    int visibles = 0;
    for (int r = 0; r < total; ++r)
        if (!ui->twRegistros->isRowHidden(r)) ++visibles;

    ui->lblTotal->setText(QStringLiteral("Mostrando %1 de %2").arg(visibles).arg(total));
    ui->lblPagina->setText(QStringLiteral("%1 / %2").arg(m_currentPage).arg(1)); // paginación simple por ahora
}

/* =================== Acciones de encabezado =================== */

void RecordsPage::onTablaChanged(int /*index*/)
{
    // Si el usuario cambia manualmente el combo, recargamos desde el modelo
    const QString t = ui->cbTabla->currentText();
    if (t.isEmpty()) return;

    const Schema s = DataModel::instance().schema(t);
    if (!s.isEmpty()) {
        setTableFromFieldDefs(t, s);
    }
}

void RecordsPage::onBuscarChanged(const QString& text)
{
    aplicarFiltroBusqueda(text);
    updateStatusLabels();
}

void RecordsPage::onLimpiarBusqueda()
{
    ui->leBuscar->clear();
    ui->leBuscar->setFocus();
}

/* ============ Operaciones Insertar / Editar / Eliminar (reales) ============ */

void RecordsPage::onInsertar()
{
    limpiarFormulario();
    setMode(Mode::Insert);
    ui->leId->setFocus();
}

void RecordsPage::onEditar()
{
    const int row = selectedRow();
    if (row < 0) return;

    limpiarFormulario();
    cargarFormularioDesdeFila(row);
    setMode(Mode::Edit);
}

void RecordsPage::onEliminar()
{
    const auto selRows = ui->twRegistros->selectionModel()->selectedRows();
    if (selRows.isEmpty() || m_tableName.isEmpty()) return;

    if (QMessageBox::question(this, tr("Eliminar"),
                              tr("¿Eliminar %1 registro(s)?").arg(selRows.size())) != QMessageBox::Yes) return;

    QList<int> rows;
    rows.reserve(selRows.size());
    for (const auto& mi : selRows) rows << mi.row();
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    DataModel::instance().removeRows(m_tableName, rows);
    emit recordDeleted(m_tableName, rows);
    updateStatusLabels();
    updateHeaderButtons();
}

void RecordsPage::onGuardar()
{
    if (m_tableName.isEmpty() || m_schema.isEmpty()) { setMode(Mode::Idle); return; }

    QString err;
    if (m_mode == Mode::Insert) {
        Record r = buildRecordFromForm();
        if (!DataModel::instance().insertRow(m_tableName, r, &err)) {
            QMessageBox::warning(this, tr("Error al insertar"), err);
            return;
        }
        emit recordInserted(m_tableName);
        // Seleccionar última
        if (ui->twRegistros->rowCount() > 0)
            ui->twRegistros->selectRow(ui->twRegistros->rowCount()-1);
    } else if (m_mode == Mode::Edit) {
        const int row = selectedRow();
        if (row >= 0) {
            Record r = buildRecordFromForm();
            if (!DataModel::instance().updateRow(m_tableName, row, r, &err)) {
                QMessageBox::warning(this, tr("Error al actualizar"), err);
                return;
            }
            emit recordUpdated(m_tableName, row);
            ui->twRegistros->selectRow(row);
        }
    }
    setMode(Mode::Idle);
    updateStatusLabels();
}

void RecordsPage::onCancelar()
{
    setMode(Mode::Idle);
}

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
}

void RecordsPage::onItemDoubleClicked()
{
    onEditar();
}

/* =================== Editor =================== */

void RecordsPage::onLimpiarFormulario()
{
    limpiarFormulario();
}

void RecordsPage::onGenerarDummyFila()
{
    // Rellena el formulario con datos de ejemplo (útil en sandbox)
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

/* =================== Utilidades de formulario/tabla =================== */

int RecordsPage::selectedRow() const
{
    const auto selRows = ui->twRegistros->selectionModel()->selectedRows();
    return selRows.isEmpty() ? -1 : selRows.first().row();
}

static QString nrm(const QString& s) { return s.toLower(); }

Record RecordsPage::buildRecordFromForm() const
{
    Record r(m_schema.size());

    int extraIdx = 0;
    auto takeExtra = [&]()->QString{
        if (extraIdx == 0) { ++extraIdx; return ui->leExtra1->text(); }
        if (extraIdx == 1) { ++extraIdx; return ui->leExtra2->text(); }
        if (extraIdx == 2) { ++extraIdx; return ui->leExtra3->text(); }
        return QString();
    };

    for (int i = 0; i < m_schema.size(); ++i) {
        const FieldDef& f = m_schema[i];
        const QString nm = nrm(f.name);

        QVariant v;
        if (nm.startsWith("id"))                            v = ui->leId->text();
        else if (nm.contains("nombre"))                     v = ui->leNombre->text();
        else if (nm.contains("correo") || nm.contains("mail")) v = ui->leCorreo->text();
        else if (nm.contains("tel"))                        v = ui->leTelefono->text();
        else if (nm.contains("fecha"))                      v = ui->deFecha->date();
        else if (nm.contains("activo") || nm.contains("estado")) v = (ui->chkActivo->isChecked() ? "1" : "0");
        else if (nm.contains("saldo") || nm.contains("moned") || nm.contains("precio"))
            v = ui->dsbSaldo->value();
        else if (nm.contains("nota") || nm.contains("obs")) v = ui->pteNotas->toPlainText();
        else                                                v = takeExtra();

        r[i] = v;
    }
    return r;
}

void RecordsPage::setFormFromRecord(const Record& rec)
{
    // Limpia primero
    limpiarFormulario();

    // Heurísticas por nombre
    for (int i = 0; i < m_schema.size(); ++i) {
        const FieldDef& f = m_schema[i];
        const QString nm = nrm(f.name);
        const QVariant v = (i < rec.size()) ? rec[i] : QVariant();

        if (nm.startsWith("id"))                            ui->leId->setText(v.toString());
        else if (nm.contains("nombre"))                     ui->leNombre->setText(v.toString());
        else if (nm.contains("correo") || nm.contains("mail")) ui->leCorreo->setText(v.toString());
        else if (nm.contains("tel"))                        ui->leTelefono->setText(v.toString());
        else if (nm.contains("fecha")) {
            if (v.canConvert<QDate>()) ui->deFecha->setDate(v.toDate());
            else ui->deFecha->setDate(QDate::fromString(v.toString(), "yyyy-MM-dd"));
        }
        else if (nm.contains("activo") || nm.contains("estado"))
            ui->chkActivo->setChecked(v.toString().trimmed() == "1" || v.toString().compare("sí", Qt::CaseInsensitive) == 0 || v.toString().compare("si", Qt::CaseInsensitive) == 0 || v.toString().compare("true", Qt::CaseInsensitive) == 0);
        else if (nm.contains("saldo") || nm.contains("moned") || nm.contains("precio"))
            ui->dsbSaldo->setValue(v.toDouble());
        else if (nm.contains("nota") || nm.contains("obs"))
            ui->pteNotas->setPlainText(v.toString());
        // Campos "extra" quedan vacíos; se pueden llenar manualmente si aplica
    }
}

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
    if (row < 0) return;
    const auto& rows = DataModel::instance().rows(m_tableName);
    if (row >= rows.size()) return;
    setFormFromRecord(rows[row]);
}

// (legacy, ya no se usa para persistir; mantenido por compatibilidad)
void RecordsPage::escribirFormularioEnFila(int /*row*/) {}
int  RecordsPage::agregarFilaDesdeFormulario() { return -1; }

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
}

/* =================== Sandbox legacy (no usados en flujo principal) =================== */

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
    for (int i = 0; i < 0; ++i) { /* vacío a propósito */ }
}
