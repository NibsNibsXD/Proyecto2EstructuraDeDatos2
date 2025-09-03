#ifndef RECORDSPAGE_H
#define RECORDSPAGE_H

#include <QWidget>
#include <QMetaObject>
#include "datamodel.h"   // FieldDef / Schema / DataModel

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

    // Estado de navegación (posición 1-based y habilitado prev/next)
    void navState(int cur, int tot, bool canPrev, bool canNext);

public slots:
    // === Integración con TablesPage/Shell ===
    // Recibe el nombre de la tabla y su esquema (lista de campos) y reconstruye la grilla
    void setTableFromFieldDefs(const QString& name, const Schema& defs);

    // --- Navegación (para la barra inferior del shell) ---
    void navFirst();
    void navPrev();
    void navNext();
    void navLast();

private slots:
    // Encabezado / acciones
    void onTablaChanged(int index);
    void onBuscarChanged(const QString& text);
    void onLimpiarBusqueda();
    void onInsertar();
    void onEditar();
    void onEliminar();
    void onGuardar();   // (no usado en el CRUD real; se mantienen por compat.)
    void onCancelar();  // (no usado en el CRUD real; se mantienen por compat.)

    // Tabla
    void onSelectionChanged();
    void onItemDoubleClicked();

    // Editor (maqueta del panel derecho)
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
    QString m_tableName;              // tabla actualmente mostrada
    Schema  m_schema;                 // esquema actual
    QMetaObject::Connection m_rowsConn; // suscripción a DataModel::rowsChanged

    // ---- Helpers de UI (sin estilos) ----
    void setMode(Mode m);
    void updateHeaderButtons();
    void updateStatusLabels();

    // Demo legacy (se conservan para modo sandbox si no hay tabla)
    void construirColumnasDemo();
    void cargarDatosDemo();

    // ---- Nuevo: carga desde DataModel ----
    void applyDefs(const Schema& defs);   // columnas según esquema
    void reloadRows();                    // filas desde DataModel::rows(...)
    QString formatCell(const FieldDef& fd, const QVariant& v) const;

    // Panel maqueta legacy
    void limpiarFormulario();
    void cargarFormularioDesdeFila(int row);
    void escribirFormularioEnFila(int row);
    int  agregarFilaDesdeFormulario();

    // Búsqueda
    bool filaCoincideBusqueda(int row, const QString& term) const;
    void aplicarFiltroBusqueda(const QString& term);

    // ---- Diálogo dinámico por esquema ----
    // Devuelve true si el usuario acepta. r es in/out.
    bool editRecordDialog(const QString& title, const Schema& s, Record& r,
                          bool isInsert, QString* errMsg = nullptr);

    // ---- Navegación visible ----
    void updateNavState();                        // emite navState(...)
    QList<int> visibleRows() const;               // índices de filas no ocultas
    int selectedVisibleIndex() const;             // posición (0-based) dentro de visibles o -1
    void selectVisibleByIndex(int visIndex);      // selecciona por índice visible (clamped)
};

#endif // RECORDSPAGE_H
