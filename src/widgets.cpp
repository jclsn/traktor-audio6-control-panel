#include "widgets.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>

namespace ui {

namespace {

const Theme kLight{
    .window = "#f3f3f3",
    .page = "#ffffff",
    .field = "#ffffff",
    .border = "#55616b",
    .groupBorder = "#8b959d",
    .strip = "#d9d9d9",
    .text = "#262626",
    .disabled = "#9da3a8",
    .accent = "#7aabdc",
    .accentBorder = "#5687bd",
    .glyph = "#2f3b45",
    .buttonTop = "#fbfcfd",
    .buttonBottom = "#dde6ee",
    .buttonDownTop = "#cfdbe6",
    .buttonDownBottom = "#e6edf3",
    .buttonOff = "#f1f1f1",
    .pressed = "#e3ecf3",
    .tabHover = "#eaf1f7",
    .bannerTop = "#e3ebf1",
    .bannerBottom = "#cfdbe5",
    .bannerEdge = "#6d7c89",
    .bannerHatch = QColor(255, 255, 255, 110),
    .badge = "#2f3b45",
    .badgeText = "#eef3f7",
    .hatchFill = "#f4f4f4",
    .hatchLine = "#ebebeb",
    .hatchEdge = "#d2d2d2",
    .menuBar = "#e4e4e4",
    .okFill = "#c6e6b4",
    .okEdge = "#4a9a38",
    .errorFill = "#f5c6c0",
    .errorEdge = "#b84a3e",
    .infoFill = "#b9e3f7",
    .infoEdge = "#4f97c1",
    .good = "#1f9d1f",
    .warn = "#d98e00",
    .bad = "#e01b1b",
};

const Theme kDark{
    .window = "#1d2125",
    .page = "#282d33",
    .field = "#1d2125",
    .border = "#73808c",
    .groupBorder = "#4a545e",
    .strip = "#39414a",
    .text = "#e4e8eb",
    .disabled = "#5f6972",
    .accent = "#4f8fd0",
    .accentBorder = "#77aee8",
    .glyph = "#d9e0e6",
    .buttonTop = "#414a54",
    .buttonBottom = "#333b44",
    .buttonDownTop = "#272d34",
    .buttonDownBottom = "#303841",
    .buttonOff = "#2b3036",
    .pressed = "#3b4652",
    .tabHover = "#2c333a",
    .bannerTop = "#3a444e",
    .bannerBottom = "#2c343c",
    .bannerEdge = "#5d6a76",
    .bannerHatch = QColor(255, 255, 255, 12),
    .badge = "#dfe6ec",
    .badgeText = "#20262c",
    .hatchFill = "#24292e",
    .hatchLine = "#2b3137",
    .hatchEdge = "#3a424a",
    .menuBar = "#262b30",
    .okFill = "#294528",
    .okEdge = "#5fae4d",
    .errorFill = "#52302c",
    .errorEdge = "#d0695c",
    .infoFill = "#23425a",
    .infoEdge = "#5ba7d3",
    .good = "#62d162",
    .warn = "#f0b03a",
    .bad = "#ff6f6f",
};

const Theme *g_theme = &kLight; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

QFont sized(const QWidget *w, int px, bool bold = false)
{
    QFont f = w->font();
    f.setPixelSize(px);

    if (bold) {
        f.setWeight(QFont::DemiBold);
    }

    return f;
}

// Crisp 1px outline: QRectF aligned on pixel centres.
QRectF crisp(const QRect &r)
{
    return QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
}

QLinearGradient buttonFill(const QRect &r, bool down)
{
    const Theme &t = theme();
    QLinearGradient g(0, r.top(), 0, r.bottom());
    g.setColorAt(0, down ? t.buttonDownTop : t.buttonTop);
    g.setColorAt(1, down ? t.buttonDownBottom : t.buttonBottom);

    return g;
}

void diagonalHatch(QPainter &p, const QRectF &r, const QColor &c, int step)
{
    p.save();
    p.setClipRect(r);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(c, 1));
    const int count = static_cast<int>((r.width() + r.height()) / step) + 1;

    for (int i = 0; i < count; ++i) {
        const qreal x = r.left() - r.height() + i * step;
        p.drawLine(QPointF(x, r.bottom()), QPointF(x + r.height(), r.top()));
    }

    p.restore();
}

} // namespace

const Theme &theme()
{
    return *g_theme;
}

bool darkMode()
{
    return g_theme == &kDark;
}

void setDarkMode(bool dark)
{
    g_theme = dark ? &kDark : &kLight;
    const Theme &t = theme();

    QPalette pal;
    pal.setColor(QPalette::Window, t.window);
    pal.setColor(QPalette::WindowText, t.text);
    pal.setColor(QPalette::Base, t.page);
    pal.setColor(QPalette::AlternateBase, t.window);
    pal.setColor(QPalette::Text, t.text);
    pal.setColor(QPalette::Button, t.buttonBottom);
    pal.setColor(QPalette::ButtonText, t.text);
    pal.setColor(QPalette::ToolTipBase, t.page);
    pal.setColor(QPalette::ToolTipText, t.text);
    pal.setColor(QPalette::Highlight, t.accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, t.disabled);
    pal.setColor(QPalette::Disabled, QPalette::Text, t.disabled);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, t.disabled);
    QApplication::setPalette(pal);

    qApp->setStyleSheet(QStringLiteral("QMenuBar { background: %1; font-weight: normal; }"
                                       "QMenuBar::item { padding: 2px 7px; background: transparent; }"
                                       "QMenuBar::item:selected { background: %2; color: white; }"
                                       "QMenu { font-weight: normal; }"
                                       "QComboBox QAbstractItemView { font-weight: bold; }")
                            .arg(t.menuBar.name(), t.accent.name()));

    for (QWidget *w : QApplication::allWidgets()) {
        w->update();
    }
}

// ---------------------------------------------------------------- Canvas

void Canvas::setActiveTab(const QRect &tab)
{
    m_activeTab = tab;
    update();
}

void Canvas::paintEvent(QPaintEvent *)
{
    const Theme &t = theme();
    QPainter p(this);
    p.fillRect(rect(), t.window);
    p.setRenderHint(QPainter::Antialiasing);

    // Title banner
    const QRectF banner = crisp(QRect(8, 9, kWidth - 16, 38));
    QPainterPath path;
    path.addRoundedRect(banner, 3, 3);
    QLinearGradient g(0, banner.top(), 0, banner.bottom());
    g.setColorAt(0, t.bannerTop);
    g.setColorAt(1, t.bannerBottom);
    p.fillPath(path, g);
    p.save();
    p.setClipPath(path);
    diagonalHatch(p, banner, t.bannerHatch, 4);
    p.restore();
    p.setPen(QPen(t.bannerEdge, 1));
    p.drawPath(path);

    p.setPen(t.text);
    p.setFont(sized(this, 12, true));
    p.drawText(banner, Qt::AlignCenter, QStringLiteral("TRAKTOR AUDIO 6 – CONTROL PANEL"));

    // Model badge on the right
    const QRectF badge(banner.right() - 38, banner.top() + 9.5, 29, 18);
    p.setPen(Qt::NoPen);
    p.setBrush(t.badge);
    p.drawRoundedRect(badge, 4, 4);
    QFont badgeFont = sized(this, 11);
    badgeFont.setWeight(QFont::Black);
    badgeFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    p.setFont(badgeFont);
    p.setPen(t.badgeText);
    p.drawText(badge.adjusted(0.5, 0, 0.5, -0.5), Qt::AlignCenter, QStringLiteral("A6"));

    // Page frame, merged with the selected side tab into a single outline
    const QRect frame(kFrameX, kFrameY, kFrameRight - kFrameX, height() - kFrameY - kFrameBottomMargin);
    QPainterPath outline;
    outline.addRoundedRect(crisp(frame), 3, 3);

    if (m_activeTab.isValid()) {
        // Rounded on the left, square where it runs into the frame, so that the
        // union leaves one clean outline around both.
        const QRectF tab = crisp(m_activeTab);
        QPainterPath shape;
        shape.addRoundedRect(tab, 3, 3);
        QPainterPath bridge;
        bridge.addRect(QRectF(tab.left() + 6, tab.top(), frame.left() + 12 - tab.left(), tab.height()));
        outline = outline.united(shape.united(bridge)).simplified();
    }

    p.setPen(QPen(t.border, 1));
    p.setBrush(t.page);
    p.drawPath(outline);
}

// ---------------------------------------------------------------- SideTab

SideTab::SideTab(const QString &text, QWidget *parent)
    : QAbstractButton(parent)
{
    setText(text);
    setCheckable(true); // exclusivity comes from a QButtonGroup in the panel
    setFocusPolicy(Qt::NoFocus);
    resize(kTabWidth + 5, kTabHeight); // the arrow of the selected tab sits past the edge
    setAttribute(Qt::WA_Hover); // repaint for the hover colour
}

void SideTab::paintEvent(QPaintEvent *)
{
    const Theme &t = theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (isChecked()) {
        // The canvas draws the shape; only the arrow pointing at the page is ours.
        QPolygonF arrow;
        arrow << QPointF(98, 10.5) << QPointF(102, 14.5) << QPointF(98, 18.5);
        p.setPen(Qt::NoPen);
        p.setBrush(t.glyph);
        p.drawPolygon(arrow);
    } else {
        p.setPen(QPen(t.border, 1));
        p.setBrush(underMouse() ? t.tabHover : t.page);
        p.drawRoundedRect(crisp(QRect(0, 0, kTabWidth, height())), 3, 3);
    }

    p.setPen(t.text);
    p.setFont(sized(this, 12));
    p.drawText(QRect(8, 0, kTabWidth - 10, height() - 1), Qt::AlignVCenter | Qt::AlignLeft, text());
}

// ---------------------------------------------------------------- SubTab

SubTab::SubTab(const QString &text, QWidget *parent)
    : QAbstractButton(parent)
{
    setText(text);
    setCheckable(true); // exclusivity comes from a QButtonGroup in the panel
    setFocusPolicy(Qt::NoFocus);
    resize(101, 29); // 22px button + room for the pointer
}

void SubTab::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r(0, 0, width(), 22);

    if (isChecked()) {
        p.setPen(QPen(theme().accentBorder, 1));
        p.setBrush(theme().accent);
        p.drawRoundedRect(crisp(r), 3, 3);
        QPolygonF tip;
        tip << QPointF(width() / 2.0 - 6, 22) << QPointF(width() / 2.0 + 6, 22) << QPointF(width() / 2.0, 28);
        p.setPen(Qt::NoPen);
        p.drawPolygon(tip);
        p.setPen(Qt::white);
    } else {
        p.setPen(QPen(theme().border, 1));
        p.setBrush(buttonFill(r, isDown()));
        p.drawRoundedRect(crisp(r), 3, 3);
        p.setPen(theme().text);
    }

    p.setFont(sized(this, 12, true));
    p.drawText(r, Qt::AlignCenter, text());
}

// ---------------------------------------------------------------- Group

void Group::addStrip(int y, const QString &text, Qt::Alignment align)
{
    m_strips.append({.y = y, .text = text, .align = align});
    update();
}

void Group::addRow(int y, QWidget *value, const QString &caption)
{
    value->setParent(this);
    value->move(19, y);
    auto *l = new QLabel(caption, this);
    l->setGeometry(127, y, width() - 137, value->height());
}

void Group::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme().groupBorder, 1));
    p.setBrush(theme().page);
    p.drawRoundedRect(crisp(rect()), 3, 3);
    p.setPen(theme().text);

    if (!m_heading.isEmpty()) {
        p.setFont(sized(this, 14, true));
        p.drawText(QRect(0, 5, width(), 20), Qt::AlignCenter, m_heading);
    }

    p.setFont(sized(this, 12, true));

    for (const Strip &s : m_strips) {
        const QRect r(10, s.y, width() - 20, 18);
        p.fillRect(r, theme().strip);
        p.drawText(r.adjusted(9, 0, -9, 0), static_cast<int>(s.align | Qt::AlignVCenter), s.text);
    }
}

// ---------------------------------------------------------------- ValueBox

ValueBox::ValueBox(QWidget *parent)
    : QWidget(parent)
{
    resize(100, 17);
}

void ValueBox::setText(const QString &t, const QColor &c)
{
    if (t == m_text && c == m_color) {
        return;
    }

    m_text = t;
    m_color = c;
    update();
}

void ValueBox::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(isEnabled() ? theme().border : theme().disabled, 1));
    p.setBrush(theme().field);
    p.drawRoundedRect(crisp(rect()), 3, 3);
    QColor textColor = theme().disabled;

    if (isEnabled()) {
        textColor = m_color.isValid() ? m_color : theme().text;
    }

    p.setPen(textColor);
    p.setFont(sized(this, 12, true));
    p.drawText(rect().adjusted(0, 0, 0, -1), Qt::AlignCenter, m_text);
}

// ---------------------------------------------------------------- Combo

Combo::Combo(QWidget *parent)
    : QComboBox(parent)
{
    setFixedSize(100, 17);
    setFocusPolicy(Qt::NoFocus);
}

void Combo::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool on = isEnabled();
    p.setPen(QPen(on ? theme().border : theme().disabled, 1));
    p.setBrush(on ? QBrush(buttonFill(rect(), false)) : QBrush(theme().buttonOff));
    p.drawRoundedRect(crisp(rect()), 3, 3);
    p.setPen(on ? theme().text : theme().disabled);
    p.setFont(sized(this, 12, true));
    p.drawText(rect().adjusted(2, 0, -17, -1), Qt::AlignCenter, currentText());
    QPolygonF arrow;
    const qreal ax = width() - 11.5;
    const qreal ay = 6.5;
    arrow << QPointF(ax - 3.5, ay) << QPointF(ax + 3.5, ay) << QPointF(ax, ay + 4.5);
    p.setPen(Qt::NoPen);
    p.setBrush(on ? theme().glyph : theme().disabled);
    p.drawPolygon(arrow);
}

// ---------------------------------------------------------------- Button

Button::Button(const QString &text, QWidget *parent)
    : QPushButton(text, parent)
{
    resize(100, 18);
    setFocusPolicy(Qt::NoFocus);
}

void Button::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool on = isEnabled();
    p.setPen(QPen(on ? theme().border : theme().disabled, 1));
    p.setBrush(on ? QBrush(buttonFill(rect(), isDown())) : QBrush(theme().buttonOff));
    p.drawRoundedRect(crisp(rect()), 3, 3);
    p.setPen(on ? theme().text : theme().disabled);
    p.setFont(sized(this, 12, true));
    p.drawText(rect().adjusted(0, 0, 0, -1), Qt::AlignCenter, text());
}

// ---------------------------------------------------------------- Check / Radio

Check::Check(const QString &text, QWidget *parent)
    : QCheckBox(text, parent)
{
    resize(110, 17);
    setFocusPolicy(Qt::NoFocus);
}

void Check::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = isEnabled() ? theme().border : theme().disabled;
    p.setPen(QPen(c, 1));
    p.setBrush(isDown() ? theme().pressed : theme().field);
    p.drawRoundedRect(QRectF(0.5, 0.5, 15, 15), 2.5, 2.5);

    if (isChecked()) {
        p.setPen(QPen(isEnabled() ? theme().glyph : theme().disabled, 1.8, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        p.drawPolyline(QPolygonF() << QPointF(4, 8.2) << QPointF(6.8, 11.2) << QPointF(12, 4.6));
    }

    p.setPen(isEnabled() ? theme().text : theme().disabled);
    p.drawText(QRect(24, 0, width() - 24, 16), Qt::AlignVCenter | Qt::AlignLeft, text());
}

Radio::Radio(const QString &text, QWidget *parent)
    : QRadioButton(text, parent)
{
    resize(110, 17);
    setFocusPolicy(Qt::NoFocus);
}

void Radio::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(isEnabled() ? theme().border : theme().disabled, 1));
    p.setBrush(isDown() ? theme().pressed : theme().field);
    p.drawEllipse(QRectF(0.5, 0.5, 15, 15));

    if (isChecked()) {
        p.setPen(Qt::NoPen);
        p.setBrush(isEnabled() ? theme().glyph : theme().disabled);
        p.drawEllipse(QPointF(8, 8), 3.5, 3.5);
    }

    p.setPen(isEnabled() ? theme().text : theme().disabled);
    p.drawText(QRect(24, 0, width() - 24, 16), Qt::AlignVCenter | Qt::AlignLeft, text());
}

// ---------------------------------------------------------------- LockButton

LockButton::LockButton(QWidget *parent)
    : QAbstractButton(parent)
{
    resize(22, 20);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

void LockButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme().border, 1));
    p.setBrush(isDown() ? theme().pressed : theme().field);
    p.drawRoundedRect(crisp(rect()), 3, 3);
    // shackle
    p.setPen(QPen(theme().glyph, 1.8));
    p.setBrush(Qt::NoBrush);
    QPainterPath sh;

    if (m_locked) {
        sh.moveTo(8, 9.5);
        sh.lineTo(8, 7.5);
        sh.arcTo(QRectF(8, 4.5, 6, 6), 180, -180);
        sh.lineTo(14, 9.5);
    } else {
        sh.moveTo(8, 9.5);
        sh.lineTo(8, 6);
        sh.arcTo(QRectF(2, 3, 6, 6), 0, 180);
        sh.lineTo(2, 7.5);
    }

    p.drawPath(sh);
    // body
    p.setPen(Qt::NoPen);
    p.setBrush(theme().glyph);
    p.drawRoundedRect(QRectF(6, 9.5, 10, 7), 1, 1);
}

// ---------------------------------------------------------------- MoonButton

MoonButton::MoonButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setToolTip("Dark mode");
    resize(20, 20);
}

void MoonButton::paintEvent(QPaintEvent *)
{
    const Theme &t = theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (underMouse()) {
        p.setPen(Qt::NoPen);
        p.setBrush(t.pressed);
        p.drawRoundedRect(rect(), 4, 4);
    }

    // Crescent: a disc with a second, shifted disc cut out of it
    QPainterPath moon;
    moon.addEllipse(QRectF(3.5, 4, 12.5, 12.5));
    QPainterPath bite;
    bite.addEllipse(QRectF(8, 1.5, 11.5, 11.5));
    const QPainterPath crescent = moon.subtracted(bite);

    p.setPen(QPen(t.glyph, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(isChecked() ? QBrush(t.glyph) : QBrush(Qt::NoBrush));
    p.drawPath(crescent);
}

// ---------------------------------------------------------------- Notice

Notice::Notice(QWidget *parent)
    : QWidget(parent)
    , lock(new LockButton(this))
    , save(new Button("Save", this))
{
    resize(646, 50);
    lock->move(10, 15);
    lock->hide();
    save->move(width() - 112, 16);
    save->hide();
}

void Notice::set(Kind kind, const QString &text)
{
    if (kind == m_kind && text == m_text) {
        return;
    }

    m_kind = kind;
    m_text = text;
    update();
}

void Notice::showLock(bool visible, bool locked)
{
    lock->setVisible(visible);
    lock->setLocked(locked);
    update();
}

void Notice::paintEvent(QPaintEvent *)
{
    const Theme &t = theme();
    QColor fill = t.okFill;
    QColor edge = t.okEdge;

    if (m_kind == Kind::Error) {
        fill = t.errorFill;
        edge = t.errorEdge;
    } else if (m_kind == Kind::Info) {
        fill = t.infoFill;
        edge = t.infoEdge;
    }

    // Icon strokes need more contrast against the white disc than the border.
    const QColor mark = darkMode() ? edge : edge.darker(120);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(edge, 1));
    p.setBrush(fill);
    p.drawRoundedRect(crisp(rect()), 3, 3);

    int textX = 10;

    if (m_kind != Kind::Info) {
        const QPointF c(21.5, 25.5);
        p.setPen(QPen(mark, 1.6));
        p.setBrush(t.field);
        p.drawEllipse(c, 10, 10);
        p.setPen(QPen(mark, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        if (m_kind == Kind::Ok) {
            p.drawPolyline(QPolygonF()
                           << c + QPointF(-4.5, 0.5) << c + QPointF(-1.2, 3.8) << c + QPointF(4.8, -3.8));
        } else {
            p.drawLine(c + QPointF(-3.8, -3.8), c + QPointF(3.8, 3.8));
            p.drawLine(c + QPointF(-3.8, 3.8), c + QPointF(3.8, -3.8));
        }

        textX = 42;
    } else if (lock->isVisible()) {
        textX = 41;
    }

    p.setPen(theme().text);
    const int right = save->isVisible() ? save->x() - 10 : width() - 10;
    p.drawText(QRect(textX, 4, right - textX, height() - 9),
               Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, m_text);
}

// ---------------------------------------------------------------- Hatch / Legend

void Hatch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(crisp(rect()), 3, 3);
    p.fillPath(path, theme().hatchFill);
    p.save();
    p.setClipPath(path);
    diagonalHatch(p, rect(), theme().hatchLine, 4);
    p.restore();
    p.setPen(QPen(theme().hatchEdge, 1));
    p.drawPath(path);
}

void Legend::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme().border, 1));
    p.setBrush(theme().field);
    p.drawRoundedRect(crisp(rect()), 3, 3);
    p.setFont(sized(this, 9, true));

    struct Row {
        const char *text = nullptr;
        QColor color;
    };

    const Row rows[] = {
        {.text = "> 1000 µs", .color = theme().bad},
        {.text = "< 1000 µs", .color = theme().warn},
        {.text = "< 500 µs", .color = theme().good},
    };

    int y = 5;

    for (const auto &r : rows) {
        p.setPen(r.color);
        p.drawText(QRect(4, y, 44, 11), Qt::AlignRight | Qt::AlignVCenter, QString::fromUtf8(r.text));
        y += 11;
    }
}

} // namespace ui
