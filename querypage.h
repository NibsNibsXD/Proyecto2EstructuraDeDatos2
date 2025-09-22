#ifndef QUERYPAGE_H
#define QUERYPAGE_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QToolButton>
#include <QLabel>
#include <QComboBox>
#include <QVariant>
#include <QDate>
#include <QEvent>
#include <QKeyEvent>
#include <QString>
#include "datamodel.h"

// Fwd decls para aligerar el header
class QToolBar;
class QAction;

class QueryPage : public QWidget {
    Q_OBJECT
public:
    explicit QueryPage(QWidget* parent=nullptr);

signals:
    // Se emiten tras guardar (para refrescar la lista en la izquierda)
    void saved(const QString& name);
    void savedAs(const QString& name);

public slots:
    // Guardado (añadidos)
    void save();
    void saveAs();
    void loadSavedByName(const QString& name); // cargar por nombre desde DataModel

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    // Existentes
    void runQuery();
    void clearEditor();
    void loadExample(int idx);
    void setSqlText(const QString& sql);

private:
    // Estructuras de parsing/ejecución (existentes)
    struct SelectSpec {
        QStringList columns; // vacío => *
        QString table;
        struct Cond { QString col; QString op; QVariant val; };
        QList<Cond> where;   // solo AND para MVP
        QString orderBy;
        bool orderDesc = false;
        int limit = -1;
    };

    struct InsertSpec { QString table; QStringList cols; QList<QVariant> vals; };
    struct DeleteSpec { QString table; SelectSpec::Cond where; bool hasWhere=false; };

    // ===== UI =====
    // Editor y resultados existentes
    QPlainTextEdit*  m_sql      = nullptr;
    QTableWidget*    m_grid     = nullptr;
    QLabel*          m_status   = nullptr;
    QComboBox*       m_examples = nullptr;

    // Barra de herramientas para guardar (nuevos)
    QToolBar* toolbar_   = nullptr;
    QAction*  actSave_   = nullptr;
    QAction*  actSaveAs_ = nullptr;

    // Estado de la consulta (nuevos)
    QString currentName_;
    QString currentSql_;

    // ===== Helpers de parsing (existentes) =====
    static QString normWS(const QString& s);
    static QString up(const QString& s);
    static QStringList splitCsv(const QString& s);
    static QVariant parseLiteral(const QString& tok);
    bool parseSelect(const QString& sql, SelectSpec& out, QString* err);
    bool parseInsert(const QString& sql, InsertSpec& out, QString* err);
    bool parseDelete(const QString& sql, DeleteSpec& out, QString* err);

    // ===== Exec (existentes) =====
    void execSelect(const SelectSpec& q);
    void execInsert(const InsertSpec& q);
    void execDelete(const DeleteSpec& q);

    // ===== Guardado (nuevos) =====
    QString collectCurrentQueryText() const;                 // obtiene SQL del editor
    bool persist(const QString& name, const QString& sql, QString* err=nullptr); // usa DataModel
};

#endif // QUERYPAGE_H
