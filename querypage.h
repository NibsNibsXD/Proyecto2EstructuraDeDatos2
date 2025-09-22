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
#include "datamodel.h"

class QueryPage : public QWidget {
    Q_OBJECT
public:
    explicit QueryPage(QWidget* parent=nullptr);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void runQuery();
    void clearEditor();
    void loadExample(int idx);
    void setSqlText(const QString& sql);

private:
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

    // UI
    QPlainTextEdit*  m_sql   = nullptr;
    QTableWidget*    m_grid  = nullptr;
    QLabel*          m_status= nullptr;
    QComboBox*       m_examples = nullptr;

    // Parsing helpers
    static QString normWS(const QString& s);
    static QString up(const QString& s);
    static QStringList splitCsv(const QString& s);
    static QVariant parseLiteral(const QString& tok);
    bool parseSelect(const QString& sql, SelectSpec& out, QString* err);
    bool parseInsert(const QString& sql, InsertSpec& out, QString* err);
    bool parseDelete(const QString& sql, DeleteSpec& out, QString* err);

    // Exec
    void execSelect(const SelectSpec& q);
    void execInsert(const InsertSpec& q);
    void execDelete(const DeleteSpec& q);
};

#endif // QUERYPAGE_H
