#include "widgets.h"

#include <QPainter>
#include <QPainterPath>

namespace ui {

const QColor kGood("#1f9d1f"), kWarn("#d98e00"), kBad("#e01b1b");

namespace {

const QColor kBorder("#55616b"); // controls, page frame
const QColor kGroupBorder("#8b959d"); // group boxes
const QColor kStrip("#d9d9d9");
const QColor kText("#262626");
const QColor kDisabled("#9da3a8");
const QColor kBlue("#7aabdc"), kBlueBorder("#5687bd");

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
    QLinearGradient g(0, r.top(), 0, r.bottom());
    g.setColorAt(0, down ? QColor("#cfdbe6") : QColor("#fbfcfd"));
    g.setColorAt(1, down ? QColor("#e6edf3") : QColor("#dde6ee"));

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

// ---------------------------------------------------------------- Canvas

void Canvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#f3f3f3"));
    p.setRenderHint(QPainter::Antialiasing);

    // Title banner
    const QRectF banner = crisp(QRect(8, 9, kWidth - 16, 38));
    QPainterPath path;
    path.addRoundedRect(banner, 3, 3);
    QLinearGradient g(0, banner.top(), 0, banner.bottom());
    g.setColorAt(0, QColor("#e3ebf1"));
    g.setColorAt(1, QColor("#cfdbe5"));
    p.fillPath(path, g);
    p.save();
    p.setClipPath(path);
    diagonalHatch(p, banner, QColor(255, 255, 255, 110), 4);
    p.restore();
    p.setPen(QPen(QColor("#6d7c89"), 1));
    p.drawPath(path);

    p.setPen(kText);
    p.setFont(sized(this, 12, true));
    p.drawText(banner, Qt::AlignCenter, QStringLiteral("TRAKTOR AUDIO 6 – CONTROL PANEL"));

    // Small maker badge on the right
    const QRectF badge(banner.right() - 36.5, banner.top() + 9, 27, 19);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#2f3b45"));
    p.drawRoundedRect(badge, 3, 3);
    p.setBrush(QColor("#dfe8ef"));
    p.drawRect(QRectF(badge.left() + 4, badge.top() + 4, 3, 11));
    QPolygonF n;
    n << QPointF(badge.left() + 7, badge.top() + 4) << QPointF(badge.left() + 10, badge.top() + 4)
      << QPointF(badge.left() + 14, badge.top() + 15) << QPointF(badge.left() + 11, badge.top() + 15);
    p.drawPolygon(n);
    p.drawRect(QRectF(badge.left() + 11, badge.top() + 4, 3, 11));
    p.drawRect(QRectF(badge.left() + 19, badge.top() + 4, 4, 11));

    // Page frame
    const QRect frame(kFrameX, kFrameY, kFrameRight - kFrameX, height() - kFrameY - kFrameBottomMargin);
    p.setPen(QPen(kBorder, 1));
    p.setBrush(Qt::white);
    p.drawRoundedRect(crisp(frame), 3, 3);
}

// ---------------------------------------------------------------- SideTab

SideTab::SideTab(const QString &text, QWidget *parent)
    : QAbstractButton(parent)
{
    setText(text);
    setCheckable(true); // exclusivity comes from a QButtonGroup in the panel
    setFocusPolicy(Qt::NoFocus);
    resize(106, 29);
}

void SideTab::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(kBorder, 1));

    if (isChecked()) {
        // Open to the right so it joins the page frame.
        const qreal r = 3;
        const qreal w = width();
        const qreal b = height() - 0.5;
        QPainterPath path;
        path.moveTo(w, 0.5);
        path.lineTo(0.5 + r, 0.5);
        path.quadTo(0.5, 0.5, 0.5, 0.5 + r);
        path.lineTo(0.5, b - r);
        path.quadTo(0.5, b, 0.5 + r, b);
        path.lineTo(w, b);
        p.fillRect(QRectF(2, 1, w - 2, height() - 2), Qt::white);
        p.setBrush(Qt::white);
        p.drawPath(path);
        QPolygonF arrow;
        arrow << QPointF(98, 10.5) << QPointF(102, 14.5) << QPointF(98, 18.5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#3b4a57"));
        p.drawPolygon(arrow);
    } else {
        const QRect r(0, 0, 101, height());
        p.setBrush(underMouse() ? QColor("#f1f6fa") : QColor(Qt::white));
        p.drawRoundedRect(crisp(r), 3, 3);
    }

    p.setPen(kText);
    p.setFont(sized(this, 12));
    p.drawText(QRect(7, 0, 90, height() - 1), Qt::AlignVCenter | Qt::AlignLeft, text());
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
        p.setPen(QPen(kBlueBorder, 1));
        p.setBrush(kBlue);
        p.drawRoundedRect(crisp(r), 3, 3);
        QPolygonF tip;
        tip << QPointF(width() / 2.0 - 6, 22) << QPointF(width() / 2.0 + 6, 22) << QPointF(width() / 2.0, 28);
        p.setPen(Qt::NoPen);
        p.drawPolygon(tip);
        p.setPen(Qt::white);
    } else {
        p.setPen(QPen(kBorder, 1));
        p.setBrush(buttonFill(r, isDown()));
        p.drawRoundedRect(crisp(r), 3, 3);
        p.setPen(kText);
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
    p.setPen(QPen(kGroupBorder, 1));
    p.setBrush(Qt::white);
    p.drawRoundedRect(crisp(rect()), 3, 3);
    p.setPen(kText);

    if (!m_heading.isEmpty()) {
        p.setFont(sized(this, 14, true));
        p.drawText(QRect(0, 5, width(), 20), Qt::AlignCenter, m_heading);
    }

    p.setFont(sized(this, 12, true));

    for (const Strip &s : m_strips) {
        const QRect r(10, s.y, width() - 20, 18);
        p.fillRect(r, kStrip);
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
    p.setPen(QPen(isEnabled() ? kBorder : kDisabled, 1));
    p.setBrush(Qt::white);
    p.drawRoundedRect(crisp(rect()), 3, 3);
    QColor textColor = kDisabled;

    if (isEnabled()) {
        textColor = m_color.isValid() ? m_color : kText;
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
    p.setPen(QPen(on ? kBorder : kDisabled, 1));
    p.setBrush(on ? QBrush(buttonFill(rect(), false)) : QBrush(QColor("#f1f1f1")));
    p.drawRoundedRect(crisp(rect()), 3, 3);
    p.setPen(on ? kText : kDisabled);
    p.setFont(sized(this, 12, true));
    p.drawText(rect().adjusted(2, 0, -17, -1), Qt::AlignCenter, on ? currentText() : QStringLiteral("-"));
    QPolygonF arrow;
    const qreal ax = width() - 11.5;
    const qreal ay = 6.5;
    arrow << QPointF(ax - 3.5, ay) << QPointF(ax + 3.5, ay) << QPointF(ax, ay + 4.5);
    p.setPen(Qt::NoPen);
    p.setBrush(on ? QColor("#3b4a57") : kDisabled);
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
    p.setPen(QPen(on ? kBorder : kDisabled, 1));
    p.setBrush(on ? QBrush(buttonFill(rect(), isDown())) : QBrush(QColor("#f1f1f1")));
    p.drawRoundedRect(crisp(rect()), 3, 3);
    p.setPen(on ? kText : kDisabled);
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
    const QColor c = isEnabled() ? kBorder : kDisabled;
    p.setPen(QPen(c, 1));
    p.setBrush(isDown() ? QColor("#e3ecf3") : QColor(Qt::white));
    p.drawRoundedRect(QRectF(0.5, 0.5, 15, 15), 2.5, 2.5);

    if (isChecked()) {
        p.setPen(QPen(isEnabled() ? QColor("#2f3b45") : kDisabled, 1.8, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        p.drawPolyline(QPolygonF() << QPointF(4, 8.2) << QPointF(6.8, 11.2) << QPointF(12, 4.6));
    }

    p.setPen(isEnabled() ? kText : kDisabled);
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
    p.setPen(QPen(isEnabled() ? kBorder : kDisabled, 1));
    p.setBrush(isDown() ? QColor("#e3ecf3") : QColor(Qt::white));
    p.drawEllipse(QRectF(0.5, 0.5, 15, 15));

    if (isChecked()) {
        p.setPen(Qt::NoPen);
        p.setBrush(isEnabled() ? QColor("#2f3b45") : kDisabled);
        p.drawEllipse(QPointF(8, 8), 3.5, 3.5);
    }

    p.setPen(isEnabled() ? kText : kDisabled);
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
    p.setPen(QPen(kBorder, 1));
    p.setBrush(isDown() ? QColor("#dbe6ee") : QColor(Qt::white));
    p.drawRoundedRect(crisp(rect()), 3, 3);
    // shackle
    p.setPen(QPen(QColor("#2f3b45"), 1.8));
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
    p.setBrush(QColor("#2f3b45"));
    p.drawRoundedRect(QRectF(6, 9.5, 10, 7), 1, 1);
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
    QColor fill("#c6e6b4");
    QColor edge("#4a9a38");

    if (m_kind == Kind::Error) {
        fill = QColor("#f5c6c0");
        edge = QColor("#b84a3e");
    } else if (m_kind == Kind::Info) {
        fill = QColor("#b9e3f7");
        edge = QColor("#4f97c1");
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(edge, 1));
    p.setBrush(fill);
    p.drawRoundedRect(crisp(rect()), 3, 3);

    int textX = 10;

    if (m_kind != Kind::Info) {
        const QPointF c(21.5, 25.5);
        p.setPen(QPen(edge.darker(115), 1.6));
        p.setBrush(Qt::white);
        p.drawEllipse(c, 10, 10);
        p.setPen(QPen(edge.darker(125), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

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

    p.setPen(kText);
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
    p.fillPath(path, QColor("#f4f4f4"));
    p.save();
    p.setClipPath(path);
    diagonalHatch(p, rect(), QColor("#ebebeb"), 4);
    p.restore();
    p.setPen(QPen(QColor("#d2d2d2"), 1));
    p.drawPath(path);
}

void Legend::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(kBorder, 1));
    p.setBrush(Qt::white);
    p.drawRoundedRect(crisp(rect()), 3, 3);
    p.setFont(sized(this, 9, true));

    struct Row {
        const char *text = nullptr;
        QColor color;
    };

    const Row rows[] = {
        {.text = "> 1000 µs", .color = kBad},
        {.text = "< 1000 µs", .color = kWarn},
        {.text = "< 500 µs", .color = kGood},
    };

    int y = 5;

    for (const auto &r : rows) {
        p.setPen(r.color);
        p.drawText(QRect(4, y, 44, 11), Qt::AlignRight | Qt::AlignVCenter, QString::fromUtf8(r.text));
        y += 11;
    }
}

} // namespace ui
