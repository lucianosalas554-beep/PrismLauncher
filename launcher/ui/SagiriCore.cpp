// SPDX-License-Identifier: GPL-3.0-only
#include "SagiriCore.h"

#include <QBuffer>
#include <QDataStream>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <cmath>

#include "Application.h"
#include "SagiriIcon.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Sagiri {

// ------------------------------------------------------------------ Config
Config& Config::get()
{
    static Config c;
    return c;
}

Config::Config() : m_s(APPLICATION->dataRoot() + "/sagiriprism.ini", QSettings::IniFormat) {}

QVariant Config::value(const QString& key, const QVariant& def) const
{
    return m_s.value(key, def);
}

void Config::setValue(const QString& key, const QVariant& v)
{
    m_s.setValue(key, v);
    emit changed();
}

QString Config::themeId() const
{
    return m_s.value("theme", "modern").toString();
}

void Config::setThemeId(const QString& id)
{
    setValue("theme", id);
}

static Theme preset(const QString& id)
{
    Theme t;
    t.id = id;
    if (id == "glass") {
        t.name = "iOS 26 Cristal";
        t.bg = QColor("#1b1f3a");
        t.panel = QColor(255, 255, 255);
        t.panel2 = QColor(255, 255, 255);
        t.text = QColor("#ffffff");
        t.muted = QColor("#d3d9f4");
        t.accent = QColor("#0a84ff");
        t.onAccent = QColor("#ffffff");
        t.radius = 22;
        t.glass = true;
    } else if (id == "steam") {
        t.name = "Steam";
        t.bg = QColor("#171a21");
        t.panel = QColor("#1b2838");
        t.panel2 = QColor("#2a475e");
        t.text = QColor("#c6d4df");
        t.muted = QColor("#8f98a0");
        t.accent = QColor("#66c0f4");
        t.onAccent = QColor("#0e1722");
        t.font = "Arial";
        t.radius = 3;
    } else if (id == "minecraft") {
        t.name = "Minecraft";
        t.bg = QColor("#2a1c11");
        t.panel = QColor("#3a3a3a");
        t.panel2 = QColor("#5a5a5a");
        t.text = QColor("#ffffff");
        t.muted = QColor("#c4c4c4");
        t.accent = QColor("#5bb037");
        t.onAccent = QColor("#ffffff");
        t.radius = 0;
    } else {
        t.id = "modern";
        t.name = "Moderno";
        t.bg = QColor("#0b1322");
        t.panel = QColor("#131e33");
        t.panel2 = QColor("#1c2a45");
        t.text = QColor("#eaf0fb");
        t.muted = QColor("#8d9bb8");
        t.accent = QColor("#3b82f6");
        t.onAccent = QColor("#ffffff");
        t.radius = 14;
    }
    return t;
}

Theme Config::theme() const
{
    const QString id = themeId();
    if (id != "custom")
        return preset(id);
    Theme t = preset("modern");
    t.id = "custom";
    t.name = "Personalizado";
    auto col = [this](const char* k, const QColor& d) {
        const QColor c(m_s.value(k).toString());
        return c.isValid() ? c : d;
    };
    t.bg = col("c_bg", t.bg);
    t.panel = col("c_panel", t.panel);
    t.panel2 = t.panel.lighter(130);
    t.text = col("c_text", t.text);
    t.accent = col("c_accent", t.accent);
    t.muted = t.text.darker(160);
    return t;
}

// -------------------------------------------------------------- Hoja de estilo
static QString rgba(const QColor& c, double a)
{
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a, 0, 'f', 2);
}

QString buildStyleSheet(const Theme& t, bool translucent, int alphaPct, const QString& fontFamily, int fontSize)
{
    const double a = translucent ? qBound(0, alphaPct, 100) / 100.0 : 1.0;
    const bool dark = t.bg.lightness() < 128 || t.glass;
    QString panel = rgba(t.panel, a);
    QString panel2 = rgba(t.panel2, translucent ? qMin(1.0, a + 0.08) : 1.0);
    QString panel2h = rgba(t.panel2.lighter(125), translucent ? qMin(1.0, a + 0.16) : 1.0);
    QString side = t.glass ? QString("rgba(0,0,0,0.28)") : rgba(t.bg.darker(112), translucent ? qMin(1.0, a + 0.1) : 1.0);
    QString input = t.glass ? QString("rgba(255,255,255,0.16)") : rgba(dark ? t.panel2.darker(125) : t.panel2, translucent ? 0.8 : 1.0);
    QString line = t.glass ? QString("rgba(255,255,255,0.38)") : (dark ? QString("rgba(255,255,255,0.09)") : QString("rgba(0,0,0,0.14)"));
    QString solid = t.glass ? QString("#20264a") : t.bg.darker(110).name();

    QString q = R"QSS(
QWidget { @FONT font-size:@SIZEpt; }
QMainWindow, #root { background:transparent; }
QLabel { color:@TEXT; background:transparent; }
QCheckBox, QRadioButton { color:@TEXT; spacing:8px; background:transparent; }
#side { background:@SIDE; border-right:1px solid @LINE; }
#logo { color:@TEXT; font-size:20px; font-weight:800; padding:6px 10px 14px 10px; }
#sec { color:@MUTED; font-size:11px; font-weight:700; padding:14px 12px 4px 12px; }
QPushButton { background:@PANEL2; color:@TEXT; border:1px solid @LINE; border-radius:@Rpx; padding:8px 16px; font-weight:600; }
QPushButton:hover { background:@PANEL2H; }
QPushButton:disabled { color:@MUTED; }
QPushButton[primary="true"] { background:@ACCENT; color:@ONACC; border:0; }
QPushButton[primary="true"]:hover { background:@ACCENTH; }
QPushButton[danger="true"] { background:#c0392b; color:#ffffff; border:0; }
QPushButton[danger="true"]:hover { background:#e0483a; }
QPushButton[nav="true"] { text-align:left; background:transparent; border:0; color:@MUTED; padding:11px 14px; }
QPushButton[nav="true"]:hover { background:@PANEL2; color:@TEXT; }
QPushButton[nav="true"]:checked { background:@ACCENTA; color:@TEXT; }
QPushButton[seg="true"]:checked { background:@ACCENT; color:@ONACC; border:0; }
QLineEdit, QSpinBox, QComboBox, QPlainTextEdit { background:@INPUT; color:@TEXT; border:1px solid @LINE; border-radius:@Rpx; padding:8px 12px; selection-background-color:@ACCENT; selection-color:@ONACC; }
QComboBox QAbstractItemView { background:@SOLID; color:@TEXT; selection-background-color:@ACCENT; border:1px solid @LINE; }
QListWidget { background:transparent; color:@TEXT; border:0; outline:0; }
QListWidget::item { padding:6px; border-radius:@Rpx; }
QListWidget::item:hover { background:@PANEL2; }
QListWidget::item:selected { background:@ACCENTA; color:@TEXT; }
#card, #panel { background:@PANEL; border:1px solid @LINE; border-radius:@R2px; }
QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; }
QScrollBar:vertical { background:transparent; width:10px; }
QScrollBar::handle:vertical { background:@PANEL2H; border-radius:5px; min-height:30px; }
QScrollBar:horizontal { background:transparent; height:10px; }
QScrollBar::handle:horizontal { background:@PANEL2H; border-radius:5px; min-width:30px; }
QScrollBar::add-line, QScrollBar::sub-line { height:0; width:0; }
QSlider::groove:horizontal { height:6px; background:@PANEL2; border-radius:3px; }
QSlider::handle:horizontal { background:@ACCENT; width:16px; margin:-5px 0; border-radius:8px; }
QTabWidget::pane { border:0; }
QTabBar::tab { background:transparent; color:@MUTED; padding:10px 18px; border-radius:@Rpx; margin-right:4px; }
QTabBar::tab:selected { background:@ACCENTA; color:@TEXT; }
QCheckBox[switch="true"]::indicator { width:40px; height:22px; border-radius:11px; background:@PANEL2H; border:1px solid @LINE; }
QCheckBox[switch="true"]::indicator:checked { background:@ACCENT; }
QToolTip { background:@SOLID; color:@TEXT; border:1px solid @LINE; padding:4px; }
)QSS";
    q.replace("@FONT", fontFamily.isEmpty() ? QString() : QString("font-family:\"%1\";").arg(fontFamily));
    q.replace("@SIZE", QString::number(qBound(8, fontSize, 18)));
    q.replace("@TEXT", t.text.name());
    q.replace("@MUTED", t.muted.name());
    q.replace("@ACCENTH", t.accent.lighter(118).name());
    q.replace("@ACCENTA", rgba(t.accent, 0.30));
    q.replace("@ACCENT", t.accent.name());
    q.replace("@ONACC", t.onAccent.name());
    q.replace("@LINE", line);
    q.replace("@PANEL2H", panel2h);
    q.replace("@PANEL2", panel2);
    q.replace("@PANEL", panel);
    q.replace("@SIDE", side);
    q.replace("@INPUT", input);
    q.replace("@SOLID", solid);
    q.replace("@R2", QString::number(t.radius + 2));
    q.replace("@R", QString::number(t.radius));
    return q;
}

// ------------------------------------------------------------ Iconos blancos
QIcon icon(const QString& name, const QColor& color, int size)
{
    static QHash<QString, QIcon> cache;
    const QString key = name + color.name(QColor::HexArgb) + QString::number(size);
    if (cache.contains(key))
        return cache.value(key);
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    auto poly = [&p](std::initializer_list<QPointF> pts, bool close) {
        QPolygonF pg;
        for (const auto& q : pts)
            pg << q;
        if (close)
            p.drawPolygon(pg);
        else
            p.drawPolyline(pg);
    };
    auto line = [&p](double x1, double y1, double x2, double y2) { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); };

    if (name == "home") {
        poly({ { 3, 11 }, { 12, 3 }, { 21, 11 } }, false);
        p.drawRect(QRectF(6, 11, 12, 9));
    } else if (name == "box") {
        poly({ { 4, 8 }, { 12, 4 }, { 20, 8 }, { 20, 17 }, { 12, 21 }, { 4, 17 } }, true);
        poly({ { 4, 8 }, { 12, 12 }, { 20, 8 } }, false);
        line(12, 12, 12, 21);
    } else if (name == "download") {
        line(12, 4, 12, 15);
        poly({ { 7, 10 }, { 12, 15 }, { 17, 10 } }, false);
        line(5, 20, 19, 20);
    } else if (name == "play") {
        p.setBrush(color);
        poly({ { 8, 5 }, { 19, 12 }, { 8, 19 } }, true);
    } else if (name == "video") {
        p.drawRoundedRect(QRectF(3, 6, 18, 12), 3, 3);
        p.setBrush(color);
        poly({ { 10, 9 }, { 15, 12 }, { 10, 15 } }, true);
    } else if (name == "news") {
        p.drawRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
        line(8, 9, 16, 9);
        line(8, 13, 16, 13);
        line(8, 17, 12, 17);
    } else if (name == "user") {
        p.drawEllipse(QPointF(12, 8), 4, 4);
        QPainterPath pp;
        pp.moveTo(4, 21);
        pp.cubicTo(4, 14, 20, 14, 20, 21);
        p.drawPath(pp);
    } else if (name == "shirt") {
        poly({ { 8, 4 }, { 4, 8 }, { 7, 11 }, { 8, 10 }, { 8, 20 }, { 16, 20 }, { 16, 10 }, { 17, 11 }, { 20, 8 }, { 16, 4 } }, true);
    } else if (name == "gear") {
        p.drawEllipse(QPointF(12, 12), 3, 3);
        p.drawEllipse(QPointF(12, 12), 6.5, 6.5);
        for (int k = 0; k < 8; ++k) {
            const double a = k * 3.14159265358979 / 4.0;
            line(12 + 6.5 * std::cos(a), 12 + 6.5 * std::sin(a), 12 + 9.5 * std::cos(a), 12 + 9.5 * std::sin(a));
        }
    } else if (name == "search") {
        p.drawEllipse(QPointF(11, 11), 6, 6);
        line(15.5, 15.5, 20, 20);
    } else if (name == "folder") {
        poly({ { 3, 6 }, { 10, 6 }, { 12, 8 }, { 21, 8 }, { 21, 19 }, { 3, 19 } }, true);
    } else if (name == "trash") {
        line(4, 7, 20, 7);
        p.drawRect(QRectF(6, 7, 12, 13));
        line(9, 4, 15, 4);
        line(10, 10, 10, 17);
        line(14, 10, 14, 17);
    } else if (name == "plus") {
        line(12, 5, 12, 19);
        line(5, 12, 19, 12);
    } else if (name == "heart") {
        QPainterPath pp;
        pp.moveTo(12, 20);
        pp.cubicTo(3, 13, 5, 5, 12, 9);
        pp.cubicTo(19, 5, 21, 13, 12, 20);
        p.drawPath(pp);
    } else if (name == "image") {
        p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
        p.drawEllipse(QPointF(9, 10), 2, 2);
        poly({ { 3, 18 }, { 9, 13 }, { 14, 17 }, { 17, 14 }, { 21, 18 } }, false);
    } else if (name == "edit") {
        poly({ { 4, 20 }, { 5, 15 }, { 16, 4 }, { 20, 8 }, { 9, 19 } }, true);
        line(14, 6, 18, 10);
    } else if (name == "check") {
        poly({ { 5, 12 }, { 10, 17 }, { 19, 7 } }, false);
    } else if (name == "store") {
        p.drawEllipse(QPointF(12, 12), 9, 9);
        poly({ { 15.5, 8.5 }, { 13.5, 13.5 }, { 8.5, 15.5 }, { 10.5, 10.5 } }, true);
    } else if (name == "palette") {
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.setBrush(color);
        p.drawEllipse(QPointF(8, 10), 1.3, 1.3);
        p.drawEllipse(QPointF(12, 7), 1.3, 1.3);
        p.drawEllipse(QPointF(16, 10), 1.3, 1.3);
    } else if (name == "volume") {
        poly({ { 4, 9 }, { 8, 9 }, { 13, 5 }, { 13, 19 }, { 8, 15 }, { 4, 15 } }, true);
        QPainterPath a;
        a.moveTo(16, 9);
        a.cubicTo(18, 11, 18, 13, 16, 15);
        p.drawPath(a);
    } else if (name == "chevl") {
        poly({ { 15, 5 }, { 8, 12 }, { 15, 19 } }, false);
    } else if (name == "chevr") {
        poly({ { 9, 5 }, { 16, 12 }, { 9, 19 } }, false);
    } else if (name == "eraser") {
        p.save();
        p.translate(12, 12);
        p.rotate(-45);
        p.drawRoundedRect(QRectF(-8, -4, 16, 8), 2, 2);
        line(-1, -4, -1, 4);
        p.restore();
    } else if (name == "bucket") {
        poly({ { 5, 11 }, { 11, 5 }, { 17, 11 }, { 11, 17 } }, true);
        p.drawEllipse(QPointF(19, 18), 1.6, 1.6);
    } else if (name == "picker") {
        line(5, 19, 14, 10);
        p.drawEllipse(QPointF(16, 8), 3, 3);
    } else if (name == "layers") {
        poly({ { 12, 4 }, { 21, 9 }, { 12, 14 }, { 3, 9 } }, true);
        poly({ { 3, 13 }, { 12, 18 }, { 21, 13 } }, false);
    } else if (name == "undo") {
        poly({ { 9, 6 }, { 4, 11 }, { 9, 16 } }, false);
        QPainterPath a;
        a.moveTo(4, 11);
        a.cubicTo(12, 6, 20, 8, 20, 18);
        p.drawPath(a);
    } else if (name == "redo") {
        poly({ { 15, 6 }, { 20, 11 }, { 15, 16 } }, false);
        QPainterPath a;
        a.moveTo(20, 11);
        a.cubicTo(12, 6, 4, 8, 4, 18);
        p.drawPath(a);
    } else {
        p.drawEllipse(QPointF(12, 12), 8, 8);
    }
    p.end();
    const QIcon ic(pm);
    cache.insert(key, ic);
    return ic;
}

QPixmap roundedPixmap(const QPixmap& src, int w, int h, int radius)
{
    const qreal dpr = 2.0;
    QPixmap out(int(w * dpr), int(h * dpr));
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, w, h), radius, radius);
    p.setClipPath(path);
    const QPixmap s = src.scaled(int(w * dpr), int(h * dpr), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    p.drawPixmap(QRectF(0, 0, w, h), s, QRectF((s.width() - w * dpr) / 2, (s.height() - h * dpr) / 2, w * dpr, h * dpr));
    return out;
}

// ------------------------------------------------------------------- Sonidos
#ifdef Q_OS_WIN
static QByteArray makeWav(const QString& n, double vol)
{
    struct Tone {
        double f, start, len, gain;
    };
    QList<Tone> tones;
    if (n == "note")
        tones = { { 880, 0, .1, 1 }, { 1175, .09, .2, 1 } };
    else if (n == "err")
        tones = { { 220, 0, .14, 1 }, { 165, .1, .2, 1 } };
    else if (n == "done")
        tones = { { 659, 0, .1, 1 }, { 880, .1, .1, 1 }, { 1175, .2, .3, 1 } };
    else if (n == "launch")
        tones = { { 330, 0, .1, 1 }, { 440, .1, .1, 1 }, { 660, .2, .22, 1 } };
    else
        tones = { { 520, 0, .07, 1 } };
    const int rate = 22050;
    double total = 0;
    for (const auto& t : tones)
        total = qMax(total, t.start + t.len + 0.05);
    const int count = int(total * rate);
    QByteArray pcm(count * 2, 0);
    auto* s = reinterpret_cast<qint16*>(pcm.data());
    for (int i = 0; i < count; ++i) {
        const double time = double(i) / rate;
        double v = 0;
        for (const auto& t : tones) {
            const double lt = time - t.start;
            if (lt < 0 || lt > t.len + 0.05)
                continue;
            const double env = qMin(1.0, lt / 0.008) * std::exp(-lt * (4.0 / t.len));
            v += std::sin(2 * 3.14159265358979 * t.f * lt) * env * t.gain;
        }
        v *= vol * 0.35;
        s[i] = qint16(qBound(-1.0, v, 1.0) * 32767);
    }
    QByteArray wav;
    QDataStream ds(&wav, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.writeRawData("RIFF", 4);
    ds << quint32(36 + pcm.size());
    ds.writeRawData("WAVEfmt ", 8);
    ds << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
    ds.writeRawData("data", 4);
    ds << quint32(pcm.size());
    ds.writeRawData(pcm.constData(), pcm.size());
    return wav;
}
#endif

void playSound(const QString& name)
{
#ifdef Q_OS_WIN
    auto& cfg = Config::get();
    if (!cfg.value("sound", true).toBool())
        return;
    static QHash<QString, QByteArray> cache;
    const double vol = cfg.value("volume", 40).toInt() / 100.0;
    const QString key = name + QString::number(int(vol * 100));
    if (!cache.contains(key))
        cache.insert(key, makeWav(name, vol));
    typedef BOOL(WINAPI * PlaySoundFn)(LPCWSTR, HMODULE, DWORD);
    static PlaySoundFn fn = [] {
        HMODULE m = LoadLibraryW(L"winmm.dll");
        return m ? reinterpret_cast<PlaySoundFn>(reinterpret_cast<void*>(GetProcAddress(m, "PlaySoundW"))) : nullptr;
    }();
    if (fn) {
        const QByteArray& d = cache[key];
        fn(reinterpret_cast<LPCWSTR>(d.constData()), nullptr, 0x0004 /*SND_MEMORY*/ | 0x0001 /*SND_ASYNC*/ | 0x0002 /*SND_NODEFAULT*/);
    }
#else
    Q_UNUSED(name)
#endif
}

QIcon appIcon()
{
    static const QIcon ic = [] {
        QPixmap pm;
        pm.loadFromData(kSagiriIcon, sizeof(kSagiriIcon));
        return QIcon(pm);
    }();
    return ic;
}

QPushButton* makeButton(QWidget* parent, const QString& text, bool primary, const std::function<void()>& fn, const QString& iconName)
{
    auto* b = new QPushButton(text, parent);
    b->setProperty("primary", primary);
    b->setCursor(Qt::PointingHandCursor);
    if (!iconName.isEmpty()) {
        b->setIcon(icon(iconName, Qt::white, 18));
        b->setIconSize(QSize(18, 18));
    }
    QObject::connect(b, &QPushButton::clicked, parent, [fn]() { fn(); });
    return b;
}

void clearLayout(QLayout* l)
{
    while (QLayoutItem* it = l->takeAt(0)) {
        delete it->widget();
        delete it;
    }
}

// ---------------------------------------------------------------------- Red
Http& Http::get()
{
    static Http n;
    return n;
}

static QNetworkRequest makeRequest(const QUrl& u)
{
    QNetworkRequest r(u);
    r.setHeader(QNetworkRequest::UserAgentHeader, "SagiriPrism/1.0 (uso personal)");
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return r;
}

void Http::bytes(const QUrl& url, QObject* ctx, const std::function<void(const QByteArray&)>& cb)
{
    auto* reply = m_nam.get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, ctx, [reply, cb]() { cb(reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray()); });
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void Http::json(const QUrl& url, QObject* ctx, const std::function<void(const QJsonDocument&)>& cb)
{
    bytes(url, ctx, [cb](const QByteArray& b) { cb(b.isEmpty() ? QJsonDocument() : QJsonDocument::fromJson(b)); });
}

void Http::post(const QUrl& url, const QByteArray& body, QObject* ctx, const std::function<void(const QJsonDocument&)>& cb)
{
    QNetworkRequest r = makeRequest(url);
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = m_nam.post(r, body);
    connect(reply, &QNetworkReply::finished, ctx, [reply, cb]() {
        const QByteArray b = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
        cb(b.isEmpty() ? QJsonDocument() : QJsonDocument::fromJson(b));
    });
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void Http::pixmap(const QUrl& url, QObject* ctx, const std::function<void(const QPixmap&)>& cb)
{
    const QString key = url.toString();
    if (m_cache.contains(key)) {
        cb(m_cache.value(key));
        return;
    }
    bytes(url, ctx, [this, key, cb](const QByteArray& b) {
        QPixmap pm;
        if (!b.isEmpty() && pm.loadFromData(b)) {
            m_cache.insert(key, pm);
            cb(pm);
        }
    });
}

}  // namespace Sagiri
