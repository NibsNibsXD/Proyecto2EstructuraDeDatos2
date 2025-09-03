#ifndef RECORDSPAGE_H
#define RECORDSPAGE_H

#include <QWidget>
#include "datamodel.h"   // Schema / FieldDef / Record

QT_BEGIN_NAMESPACE
namespace Ui { class RecordsPage; }
QT_END_NAMESPACE

class RecordsPage : public QWidget
{
    Q_OBJECT
public:
    explicit RecordsPage(QWidget* parent = nullptr);
    ~RecordsPage();

signals:
    // Señales listas para la integración real más adelante:
    void recordInserted(const QString& tabla);
    void recordUpdated(const QString& tabla, int row);
    void recordDeleted(const QString& tabla, const QList<int>& rows);

public slots:
    // === Integración con TablesPage/Shell ===
    // Recibe el nombre de la tabla y su esquema (lista de campos) y reconstruye la grilla
    void setTableFromFieldDefs(const QString& name, const Schema& defs);

private slots:
    // Encabezado / acciones
    void onTablaChanged(int index);
    void onBuscarChanged(const QString& text);
    void onLimpiarBusqueda();
    void onInsertar();
    void onEditar();
    void onEliminar();
    void onGuardar();
    void onCancelar();

    // Tabla
    void onSelectionChanged();
    void onItemDoubleClicked();

    // Editor
    void onLimpiarFormulario();
    void onGenerarDummyFila();

    // Paginación (solo actualiza etiqueta por ahora)
    void onPrimero();
    void onAnterior();
    void onSiguiente();
    void onUltimo();

private:
    enum class Mode { Idle, Insert, Edit };

    Ui::RecordsPage* ui = nullptr;
    Mode m_mode = Mode::Idle;
    int  m_currentPage = 1;

    // === Estado de integración ===
    QString m_tableName;   // tabla actualmente mostrada
    Schema  m_schema;      // esquema actual

    // ---- Helpers de UI ----
    void setMode(Mode m);
    void updateHeaderButtons();
    void updateStatusLabels();

    // Reconstruye columnas/filas usando el esquema recibido
    void applyDefs(const Schema& defs);
    void reloadRowsFromModel();              // vuelve a llenar la QTableWidget desde DataModel

    // Formulario genérico basado en heurísticas por nombre de campo
    Record buildRecordFromForm() const;      // construye Record alineado al Schema
    void   setFormFromRecord(const Record&); // vuelca Record al formulario

    // Utilidades varias
    int  selectedRow() const;
    bool filaCoincideBusqueda(int row, const QString& term) const;
    void aplicarFiltroBusqueda(const QString& term);

    // --- (Solo para sandbox legacy; ya no se usan, pero las dejamos por compatibilidad) ---
    void construirColumnasDemo();
    void cargarDatosDemo();

    // Limpiar/llenar form legacy
    void limpiarFormulario();
    void cargarFormularioDesdeFila(int row);     // ahora usa DataModel
    void escribirFormularioEnFila(int row);      // ya no se usa (operamos con DataModel)
    int  agregarFilaDesdeFormulario();           // ya no se usa (operamos con DataModel)
};

#endif // RECORDSPAGE_H
