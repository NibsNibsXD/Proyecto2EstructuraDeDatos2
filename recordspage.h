#ifndef RECORDSPAGE_H
#define RECORDSPAGE_H

#include <QWidget>

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

    Ui::RecordsPage* ui;
    Mode m_mode = Mode::Idle;
    int  m_currentPage = 1;

    // ---- Helpers de UI (sin estilos) ----
    void setMode(Mode m);
    void updateHeaderButtons();
    void updateStatusLabels();
    void construirColumnasDemo();       // columnas de muestra
    void cargarDatosDemo();             // filas de muestra
    void limpiarFormulario();
    void cargarFormularioDesdeFila(int row);
    void escribirFormularioEnFila(int row);
    int  agregarFilaDesdeFormulario();

    // Utilidades
    bool filaCoincideBusqueda(int row, const QString& term) const;
    void aplicarFiltroBusqueda(const QString& term);
};

#endif // RECORDSPAGE_H
