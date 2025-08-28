#include "recordspage.h"
#include "ui_recordspage.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QDate>
#include <QTableWidgetItem>
#include <QSignalBlocker>
#include <algorithm>        // std::sort
#include "datamodel.h"      // Schema / FieldDef

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

    // Configuración base de la tabla (lo visual fino se hará por stylesheet)
    ui->twRegistros->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->twRegistros->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->twRegistros->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->twRegistros->setAlternatingRowColors(true);
    ui->twRegistros->setSortingEnabled(true);
    ui->twRegistros->verticalHeader()->setVisible(false);
    ui->twRegistros->horizontalHeader()->setStretchLastSection(true);

    // Tablas demo para el sandbox
    ui->cbTabla->clear();
    ui->cbTabla->addItems(QStringList() << "Estudiantes" << "Cursos" << "Inscripciones");

    construirColumnasDemo();
    cargarDatosDemo();
    setMode(Mode::Idle);
    updateStatusLabels();
}

RecordsPage::~RecordsPage()
{
    delete ui;
}

/* =================== Integración con TablesPage/Shell =================== */

// Recibe nombre y esquema real; ajusta combo, título y grilla.
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

// Reconstruye columnas y rellena datos dummy según tipos
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

    // Demo: 20 filas de ejemplo acordes al tipo
    for (int i = 0; i < 20; ++i) {
        int row = ui->twRegistros->rowCount();
        ui->twRegistros->insertRow(row);
        for (int c = 0; c < defs.size(); ++c) {
            const FieldDef& fd = defs[c];
            QString val;
            if (fd.type == "Autonumeración" || fd.type == "Número")
                val = QString::number(1000 + i*10 + c);
            else if (fd.type == "Fecha/Hora")
                val = QDate::currentDate().addDays(-i).toString("yyyy-MM-dd");
            else if (fd.type == "Moneda")
                val = QString::number((i*3+c)*10);
            else
                val = QString("Texto %1").arg(i+1);

            ui->twRegistros->setItem(row, c, new QTableWidgetItem(val));
        }
    }
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
    const bool haveSel = !ui->twRegistros->selectedItems().isEmpty();
    const bool editing = (m_mode != Mode::Idle);

    ui->btnInsertar->setEnabled(!editing);
    ui->btnEditar->setEnabled(!editing && haveSel);
    ui->btnEliminar->setEnabled(!editing && haveSel);
    ui->btnGuardar->setEnabled(editing);
    ui->btnCancelar->setEnabled(editing);
}

void RecordsPage::updateStatusLabels()
{
    // Total visibles (no filtrados)
    int total = ui->twRegistros->rowCount();
    int visibles = 0;
    for (int r = 0; r < total; ++r)
        if (!ui->twRegistros->isRowHidden(r)) ++visibles;

    ui->lblTotal->setText(QStringLiteral("Mostrando %1 de %2").arg(visibles).arg(total));
    ui->lblPagina->setText(QStringLiteral("%1 / %2").arg(m_currentPage).arg(1)); // paginación simple por ahora
}

/* =================== Construcción demo (sandbox) =================== */

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
    // 30 filas de ejemplo
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
    // En sandbox: reconstruimos columnas y datos demo.
    construirColumnasDemo();
    cargarDatosDemo();
    ui->leBuscar->clear();
    setMode(Mode::Idle);
    updateStatusLabels();
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

/* ============ Operaciones Insertar / Editar / Eliminar ============ */

void RecordsPage::onInsertar()
{
    limpiarFormulario();
    setMode(Mode::Insert);
    ui->leId->setFocus();
}

void RecordsPage::onEditar()
{
    const auto selRows = ui->twRegistros->selectionModel()->selectedRows();
    if (selRows.isEmpty()) return;

    limpiarFormulario();
    cargarFormularioDesdeFila(selRows.first().row());
    setMode(Mode::Edit);
}

void RecordsPage::onEliminar()
{
    const auto selRows = ui->twRegistros->selectionModel()->selectedRows();
    if (selRows.isEmpty()) return;

    if (QMessageBox::question(this, tr("Eliminar"),
                              tr("¿Eliminar %1 registro(s) de la vista? (Simulado)")
                                  .arg(selRows.size())) != QMessageBox::Yes) return;

    // Eliminar de la tabla (solo visual, sandbox)
    QList<int> rows;
    rows.reserve(selRows.size());
    for (const auto& mi : selRows) rows << mi.row();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) ui->twRegistros->removeRow(r);

    emit recordDeleted(ui->cbTabla->currentText(), rows);
    updateStatusLabels();
    updateHeaderButtons();
}

void RecordsPage::onGuardar()
{
    if (m_mode == Mode::Insert) {
        int newRow = agregarFilaDesdeFormulario();
        emit recordInserted(ui->cbTabla->currentText());
        ui->twRegistros->selectRow(newRow);
    } else if (m_mode == Mode::Edit) {
        const auto selRows = ui->twRegistros->selectionModel()->selectedRows();
        if (!selRows.isEmpty()) {
            const int row = selRows.first().row();
            escribirFormularioEnFila(row);
            emit recordUpdated(ui->cbTabla->currentText(), row);
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
    if (row < 0 || row >= ui->twRegistros->rowCount()) return;

    auto get = [&](int c){ return ui->twRegistros->item(row, c)
                                       ? ui->twRegistros->item(row, c)->text() : QString(); };

    ui->leId->setText(get(0));
    ui->leNombre->setText(get(1));
    ui->leCorreo->setText(get(2));
    ui->leTelefono->setText(get(3));
    ui->deFecha->setDate(QDate::fromString(get(4), "yyyy-MM-dd"));
    ui->chkActivo->setChecked(get(5).trimmed().compare("Sí", Qt::CaseInsensitive) == 0);
    ui->dsbSaldo->setValue(0.0);
    ui->pteNotas->clear();
    ui->leExtra1->clear();
    ui->leExtra2->clear();
    ui->leExtra3->clear();
}

void RecordsPage::escribirFormularioEnFila(int row)
{
    if (row < 0 || row >= ui->twRegistros->rowCount()) return;

    auto put = [&](int c, const QString& v){
        QTableWidgetItem* it = ui->twRegistros->item(row, c);
        if (!it) { it = new QTableWidgetItem; ui->twRegistros->setItem(row, c, it); }
        it->setText(v);
    };

    put(0, ui->leId->text());
    put(1, ui->leNombre->text());
    put(2, ui->leCorreo->text());
    put(3, ui->leTelefono->text());
    put(4, ui->deFecha->date().toString("yyyy-MM-dd"));
    put(5, ui->chkActivo->isChecked() ? "Sí" : "No");
}

int RecordsPage::agregarFilaDesdeFormulario()
{
    const int row = ui->twRegistros->rowCount();
    ui->twRegistros->insertRow(row);
    escribirFormularioEnFila(row);
    return row;
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
}
