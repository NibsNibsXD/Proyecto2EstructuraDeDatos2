#ifndef RECORDSPAGE_H
#define RECORDSPAGE_H

#include <QWidget>
#include <QMetaObject>
#include "datamodel.h"   // FieldDef / Schema / DataModel
#include <QTableWidget>
#include <QToolButton>

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
public:
    void showRequiredPopup(const QModelIndex& ix, const QString& msg, int msec = 1800);
    // Nombre de la tabla actual (solo lectura)
    const QString& tableName() const { return m_tableName; }

    // Mapeo vista->modelo para el delegado
    int viewRowToDataRow(int vr) const {
        return (vr >= 0 && vr < m_rowMap.size()) ? m_rowMap[vr] : -1;
    }


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
    void showFilterMenu();


    const Schema& schema() const;
    QTableWidget* sheet() const;
    bool hasUnfilledRequired(QModelIndex* where = nullptr) const;


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

    void onCurrentCellChanged(int currentRow, int currentCol,
                              int previousRow, int previousCol);

private:
    enum class Mode { Idle, Insert, Edit };
    int m_lastTablaIndex = -1;
    bool requiredEmptyInRow(int vrow, int* whichCol = nullptr) const;

    bool m_reqPopupBusy = false;
    void refocusCellQueued(int row, int col);





    // (Si aún no existe) recordar la última opción elegida en el menú de filtro
    QString m_lastFilterValue;



    // Mapea fila visible (vista) -> fila real en DataModel
    QVector<int> m_rowMap;

    inline int dataRowForView(int vr) const {
        return (vr >= 0 && vr < m_rowMap.size()) ? m_rowMap[vr] : -1;
    }


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

    QToolButton* m_btnEliminar = nullptr;
    void onEliminarSeleccion();

    // ---- Helpers de UI ----
    void setMode(RecordsPage::Mode m);  // ← firma explícita para enlazador
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
    void updateFilterStatus(const QString &filterText);

    // Navegación visible (legacy)
    void updateNavState();
    QList<int> visibleRows() const;
    int  selectedVisibleIndex() const;
    void selectVisibleByIndex(int visIndex);
    int  m_lastSortColumn = -1;
    int  currentSortColumn() const;

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
