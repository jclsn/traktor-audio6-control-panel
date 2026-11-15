#include "backend.h"
#include "panel.h"

#include <QApplication>
#include <QStyleFactory>
#include <QTimer>

#include <cstdio>
#include <cstring>

namespace {

void usage()
{
    std::puts("Usage: traktor-audio6-panel [OPTION]\n"
              "Traktor Audio 6 Control Panel for Linux.\n\n"
              "  (no option)       open the control panel\n"
              "  --demo            open the panel against a simulated interface\n"
              "  --version         print the version\n"
              "  --help            show this help");
}

} // namespace

int main(int argc, char **argv)
{
    bool demo = false;
    const char *shotDir = nullptr;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!std::strcmp(a, "--version")) {
            std::puts(APP_VERSION);
            return 0;
        }

        if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) {
            usage();
            return 0;
        }

        if (!std::strcmp(a, "--demo")) {
            demo = true;
        } else if (!std::strcmp(a, "--screenshot") && i + 1 < argc) { // development aid
            shotDir = argv[++i];
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName("traktor-audio6-panel");
    QApplication::setDesktopFileName("traktor-audio6-panel");

    // Fixed light look, independent of the desktop theme, like the original.
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QPalette pal;
    const QColor text("#262626");
    pal.setColor(QPalette::Window, QColor("#f3f3f3"));
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, Qt::white);
    pal.setColor(QPalette::AlternateBase, QColor("#f3f3f3"));
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, QColor("#e8e8e8"));
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::ToolTipBase, Qt::white);
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::Highlight, QColor("#7aabdc"));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#9da3a8"));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor("#9da3a8"));
    QApplication::setPalette(pal);

    QFont font("Noto Sans");
    font.setStyleHint(QFont::SansSerif);
    font.setPixelSize(12);
    font.setWeight(QFont::Medium);
    QApplication::setFont(font);
    app.setStyleSheet("QMenuBar { background: #d6d3ce; font-weight: normal; }"
                      "QMenuBar::item { padding: 2px 7px; background: transparent; }"
                      "QMenuBar::item:selected { background: #0a246a; color: white; }"
                      "QMenu { font-weight: normal; }"
                      "QComboBox QAbstractItemView { font-weight: bold; }");

    Panel panel(demo);
    panel.show();

    if (shotDir) {
        const QString dir = QString::fromLocal8Bit(shotDir);
        QTimer::singleShot(200, &panel, [&panel, dir] {
            panel.saveScreenshots(dir);
            QApplication::quit();
        });
    }

    return QApplication::exec();
}
