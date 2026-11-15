#include "backend.h"
#include "panel.h"

#include <QApplication>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

enum class Scheme : std::uint8_t { Auto, Light, Dark };

void usage()
{
    std::puts("Usage: traktor-audio6-panel [OPTION]\n"
              "Traktor Audio 6 Control Panel for Linux.\n\n"
              "  (no option)       open the control panel\n"
              "  --demo            open the panel against a simulated interface\n"
              "  --dark, --light   force the colour scheme for this run\n"
              "  --version         print the version\n"
              "  --help            show this help");
}

} // namespace

int main(int argc, char **argv)
{
    bool demo = false;
    Scheme scheme = Scheme::Auto;
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
        } else if (!std::strcmp(a, "--dark")) {
            scheme = Scheme::Dark;
        } else if (!std::strcmp(a, "--light")) {
            scheme = Scheme::Light;
        } else if (!std::strcmp(a, "--screenshot") && i + 1 < argc) { // development aid
            shotDir = argv[++i];
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName("traktor-audio6-panel");
    QApplication::setDesktopFileName("traktor-audio6-panel");

    // Own look in both colour schemes, independent of the desktop's widget style.
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QFont font("Noto Sans");
    font.setStyleHint(QFont::SansSerif);
    font.setPixelSize(12);
    font.setWeight(QFont::Medium);
    QApplication::setFont(font);

    // Colour scheme: command line, then the saved choice, then the desktop setting.
    if (scheme == Scheme::Auto) {
        const QVariant saved = Panel::savedDarkMode();
        if (saved.isValid()) {
            scheme = saved.toBool() ? Scheme::Dark : Scheme::Light;
        }
    }

    const bool followSystem = scheme == Scheme::Auto;
    const bool dark = followSystem ? QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
                                   : scheme == Scheme::Dark;
    ui::setDarkMode(dark);

    Panel panel(demo, followSystem);
    panel.show();

    if (shotDir) {
        const QString dir = QString::fromLocal8Bit(shotDir);
        QTimer::singleShot(200, &panel, [&panel, dir] {
            panel.saveScreenshots(dir);
            QApplication::quit();
        });
    } else {
        QTimer::singleShot(300, &panel, &Panel::checkRealtimePriority); // once the window is up
    }

    return QApplication::exec();
}
