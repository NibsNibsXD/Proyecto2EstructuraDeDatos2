#ifndef RECORDSPAGE_H
#define RECORDSPAGE_H

#include <QWidget>
#include <QMetaObject>
#include "datamodel.h"   // FieldDef / Schema / DataModel

class QTableWidgetItem;

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
    void recordInserted(const QString& tabla);
    void recordUpdated(const QString& tabla, int row);
    void recordDeleted(const QString& tabla, const QList<int>& rows);
    void navState(int cur, int tot, bool canPrev, bool canNext);

public slots:
    // Integración con TablesPage/Shell
    void setTableFromFieldDefs(const QString& name, const Schema& defs);

    // Navegación (no visible; compat.)
    void navFirst();
    void navPrev();
    void navNext();
    void navLast();

    // Ordenación (desde Ribbon)
    void sortAscending();
    void sortDescending();
    void clearSorting();
    void showFilterMenu(); // abre menú de filtro


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
    void onItemDoubleClicked(QTableWidgetItem *item);

    // Editor legacy (oculto)
    void onLimpiarFormulario();
    void onGenerarDummyFila();

    // Paginación legacy (oculta)
    void onPrimero();
    void onAnterior();
    void onSiguiente();
    void onUltimo();
    void onCurrentCellChanged(int currentRow, int currentCol, int previousRow, int previousCol);


private:
    enum class Mode { Idle, Insert, Edit };
    // Anti-reentradas / estado interno
    bool m_isReloading  = false;
    bool m_isCommitting = false;

    Ui::RecordsPage* ui = nullptr;
    Mode m_mode = Mode::Idle;
    int  m_currentPage = 1;

    // Estado de integración
    QString m_tableName;
    Schema  m_schema;
    QMetaObject::Connection m_rowsConn;

    // ---- Helpers de UI ----
    void setMode(Mode m);
    void updateHeaderButtons();
    void updateStatusLabels();

    // Demo legacy (no se usan)
    void construirColumnasDemo();
    void cargarDatosDemo();

    // Carga real desde DataModel
    void applyDefs(const Schema& defs);
    void reloadRows();
    QString formatCell(const FieldDef& fd, const QVariant& v) const;

    // Panel legacy
    void limpiarFormulario();
    void cargarFormularioDesdeFila(int row);
    void escribirFormularioEnFila(int row);
    int  agregarFilaDesdeFormulario();

    // Búsqueda
    bool filaCoincideBusqueda(int row, const QString& term) const;
    void aplicarFiltroBusqueda(const QString& term);

    // Navegación visible (legacy)
    void updateNavState();
    QList<int> visibleRows() const;
    int  selectedVisibleIndex() const;
    void selectVisibleByIndex(int visIndex);
    int  m_lastSortColumn = -1;
    int  currentSortColumn() const;

    // --- Sorting (vista -> modelo) ---
    bool m_sortActive = false;
    int  m_sortCol = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    QVector<int>  m_rowOrder;                // índice de fila en vista -> índice en DataModel

    int  modelRowForView(int viewRow) const; // helper
    void recomputeRowOrder();                // recalcula m_rowOrder con el DataModel actual


    // --- Filtering ---
    QMap<int, QSet<QString>> m_activeFilters; // columna -> conjunto de valores permitidos especiales ("__BLANK__")
    bool rowPassesFilters(int modelRow) const;
    void applyActiveFilters();




    // --- Nueva fila editable tipo Access ---
    void addNewRowEditors(qint64 presetId = -1);
    QWidget* makeEditorFor(const FieldDef& fd) const;       // editor por tipo
    QVariant editorValue(QWidget* w, const QString& t) const;
    void clearNewRowEditors();                              // limpia la fila (para próximo insert)
    void commitNewRow();                                    // inserta en DataModel

    // Diálogo CRUD (opcional)
    bool editRecordDialog(const QString& title,
                          const Schema& s,
                          Record& r,
                          bool isInsert,
                          QString* errMsg);

    // --- Inline insert & edit ---
    bool m_preparedNextNew = false;                 // ¿ya agregamos la “siguiente (New)”?
    void prepareNextNewRow();                       // agrega otra fila (New) al empezar a escribir
    Record rowToRecord(int row) const;              // vuelca celdas -> Record para updateRow
    void onItemChanged(QTableWidgetItem* it);       // edición inline de items (no-booleanos)

    // Modo “Datasheet mínimo”: ocultar controles legacy
    void hideLegacyChrome();
};

#endif // RECORDSPAGE_H
