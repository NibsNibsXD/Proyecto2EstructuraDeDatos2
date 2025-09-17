#include <QApplication>
#include "shellwindow.h"

int main(int argc, char* argv[]) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);

#ifdef Q_OS_WIN
    // Estilo estable (evita rarezas de tema claro/oscuro del SO)
    QApplication::setStyle("Fusion");

    // Paleta clara + texto negro por defecto
    QPalette pal = qApp->palette();
    pal.setColor(QPalette::Window,      QColor(0xF0,0xF0,0xF0));
    pal.setColor(QPalette::Base,        Qt::white);
    pal.setColor(QPalette::AlternateBase, QColor(245,245,245));
    pal.setColor(QPalette::Text,        Qt::black);
    pal.setColor(QPalette::WindowText,  Qt::black);
    pal.setColor(QPalette::ButtonText,  Qt::black);
    pal.setColor(QPalette::ToolTipBase, Qt::white);
    pal.setColor(QPalette::ToolTipText, Qt::black);
    qApp->setPalette(pal);

    // Color por defecto para cualquier widget que no tenga stylesheet propio
    qApp->setStyleSheet("QWidget { color: #111; }");

    // Fuente segura en Windows
    qApp->setFont(QFont("Segoe UI", 9));
#endif

    ShellWindow w;
    w.resize(1400, 800);
    w.show();
    return app.exec();
}
