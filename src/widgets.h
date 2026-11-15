// Hand-painted widgets that reproduce the look of the original NI control panel.
#pragma once
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QWidget>

#include <cstdint>

namespace ui {

// Layout of the window content (below the menu bar), in pixels.
inline constexpr int kWidth = 805;
inline constexpr int kFrameX = 113;
inline constexpr int kFrameY = 55;
inline constexpr int kFrameRight = 797;
inline constexpr int kFrameBottomMargin = 8;
inline constexpr int kTabX = 8;
inline constexpr int kTabWidth = 101;
inline constexpr int kTabHeight = 29;
inline constexpr int kTabPitch = 33;

// Colours of every hand-painted widget; there is a light and a dark set.
struct Theme {
    QColor window, page, field, border, groupBorder, strip, text, disabled;
    QColor accent, accentBorder, glyph;
    QColor buttonTop, buttonBottom, buttonDownTop, buttonDownBottom, buttonOff, pressed, tabHover;
    QColor bannerTop, bannerBottom, bannerEdge, bannerHatch, badge, badgeText;
    QColor hatchFill, hatchLine, hatchEdge, menuBar;
    QColor okFill, okEdge, errorFill, errorEdge, infoFill, infoEdge;
    QColor good, warn, bad; // traffic-light text colours
};

const Theme &theme();
bool darkMode();

// Switches the colour set and restyles the whole application.
void setDarkMode(bool dark);

// Background with the title banner and the page frame. The selected side tab
// is drawn here as part of the frame outline, so the two join without a seam.
class Canvas : public QWidget {
public:
    using QWidget::QWidget;
    void setActiveTab(const QRect &tab);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QRect m_activeTab;
};

// Page selector on the left; the checked one only paints its label and arrow.
class SideTab : public QAbstractButton {
public:
    SideTab(const QString &text, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;
};

// "Current" / "Startup" selector; the checked one is blue with a pointer below.
class SubTab : public QAbstractButton {
public:
    SubTab(const QString &text, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;
};

// Bordered box with grey caption strips.
class Group : public QWidget {
public:
    explicit Group(QWidget *parent)
        : QWidget(parent)
    {
    }

    void setHeading(const QString &t) { m_heading = t; }

    void addStrip(int y, const QString &text, Qt::Alignment align = Qt::AlignHCenter);
    // Places `value` in the left column and a caption next to it.
    void addRow(int y, QWidget *value, const QString &caption);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    struct Strip {
        int y;
        QString text;
        Qt::Alignment align;
    };

    QList<Strip> m_strips;
    QString m_heading;
};

// Read-only value display.
class ValueBox : public QWidget {
public:
    explicit ValueBox(QWidget *parent = nullptr);
    void setText(const QString &t, const QColor &c = QColor());

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString m_text = "-";
    QColor m_color;
};

class Combo : public QComboBox {
public:
    explicit Combo(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
};

class Button : public QPushButton {
public:
    explicit Button(const QString &text, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
};

class Check : public QCheckBox {
public:
    Check(const QString &text, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;

    bool hitButton(const QPoint &p) const override { return rect().contains(p); }
};

class Radio : public QRadioButton {
public:
    Radio(const QString &text, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;

    bool hitButton(const QPoint &p) const override { return rect().contains(p); }
};

class LockButton : public QAbstractButton {
public:
    explicit LockButton(QWidget *parent);

    void setLocked(bool l)
    {
        m_locked = l;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    bool m_locked = true;
};

// Moon icon that toggles the dark mode; it is filled while dark mode is on.
class MoonButton : public QAbstractButton {
public:
    explicit MoonButton(QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;
};

// Coloured message bar at the bottom of each page.
class Notice : public QWidget {
public:
    enum class Kind : std::uint8_t { Ok, Error, Info };

    explicit Notice(QWidget *parent);
    void set(Kind kind, const QString &text);
    void showLock(bool visible, bool locked = true);

    LockButton *lock;
    Button *save;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Kind m_kind = Kind::Ok;
    QString m_text;
};

// The empty hatched area on the Channel Setup page.
class Hatch : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override;
};

// Colour legend of the System Performance Test.
class Legend : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override;
};

} // namespace ui
