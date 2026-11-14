// SPDX-License-Identifier: GPL-3.0-only
#include "SkinEditorDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
struct Region {
    int x, y, w, h;
    const char* name;
};
// Zonas de la textura 64x64 (formato moderno). El relleno se limita a la zona donde haces clic.
const QList<Region>& regions()
{
    static const QList<Region> r = {
        { 0, 0, 32, 16, QT_TR_NOOP("Cabeza") },     { 32, 0, 32, 16, QT_TR_NOOP("Sombrero") },
        { 0, 16, 16, 16, QT_TR_NOOP("Pierna D") },   { 16, 16, 24, 16, QT_TR_NOOP("Torso") },
        { 40, 16, 16, 16, QT_TR_NOOP("Brazo D") },   { 0, 32, 16, 16, QT_TR_NOOP("Pantalon D") },
        { 16, 32, 24, 16, QT_TR_NOOP("Chaqueta") },  { 40, 32, 16, 16, QT_TR_NOOP("Manga D") },
        { 16, 48, 16, 16, QT_TR_NOOP("Pierna I") },  { 32, 48, 16, 16, QT_TR_NOOP("Brazo I") },
        { 0, 48, 16, 16, QT_TR_NOOP("Pantalon I") }, { 48, 48, 16, 16, QT_TR_NOOP("Manga I") },
    };
    return r;
}
}  // namespace

// ---------------------------------------------------------------- PixelCanvas
PixelCanvas::PixelCanvas(QWidget* parent) : QWidget(parent)
{
    m_img = QImage(64, 64, QImage::Format_ARGB32);
    m_img.fill(Qt::transparent);
    setMinimumSize(320, 320);
    setCursor(Qt::CrossCursor);
    setMouseTracking(false);
}

void PixelCanvas::setImage(const QImage& img)
{
    m_img = img.convertToFormat(QImage::Format_ARGB32);
    m_undo.clear();
    m_redo.clear();
    update();
}

void PixelCanvas::setShowGrid(bool v)
{
    m_grid = v;
    update();
}

void PixelCanvas::setShowGuide(bool v)
{
    m_guide = v;
    update();
}

QRect PixelCanvas::target() const
{
    int side = qMin(width(), height());
    side -= side % 64;  // escala entera: cada pixel de la skin es un cuadrado exacto
    if (side < 64)
        side = 64;
    return QRect((width() - side) / 2, (height() - side) / 2, side, side);
}

QPoint PixelCanvas::pixelAt(const QPoint& pos) const
{
    const QRect r = target();
    if (!r.contains(pos))
        return QPoint(-1, -1);
    return QPoint((pos.x() - r.x()) * 64 / r.width(), (pos.y() - r.y()) * 64 / r.height());
}

void PixelCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect r = target();
    const int cell = qMax(4, r.width() / 32);
    for (int y = 0; y < r.height(); y += cell) {
        for (int x = 0; x < r.width(); x += cell) {
            const bool odd = ((x / cell) + (y / cell)) % 2;
            p.fillRect(QRect(r.x() + x, r.y() + y, qMin(cell, r.width() - x), qMin(cell, r.height() - y)),
                       odd ? QColor(0x38, 0x38, 0x38) : QColor(0x4a, 0x4a, 0x4a));
        }
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(r, m_img);

    const double s = r.width() / 64.0;
    if (m_grid && s >= 6) {
        p.setPen(QColor(255, 255, 255, 36));
        for (int i = 0; i <= 64; ++i) {
            const int o = qRound(i * s);
            p.drawLine(r.x() + o, r.y(), r.x() + o, r.bottom());
            p.drawLine(r.x(), r.y() + o, r.right(), r.y() + o);
        }
    }
    if (m_guide) {
        QFont f = font();
        f.setPixelSize(10);
        p.setFont(f);
        for (const auto& g : regions()) {
            const QRectF rr(r.x() + g.x * s, r.y() + g.y * s, g.w * s, g.h * s);
            p.setPen(QColor(255, 200, 60, 210));
            p.drawRect(rr);
            p.setPen(QColor(255, 215, 90));
            p.drawText(rr.adjusted(3, 1, 0, 0), Qt::AlignLeft | Qt::AlignTop, tr(g.name));
        }
    }
}

void PixelCanvas::pushUndo()
{
    m_undo.append(m_img);
    if (m_undo.size() > 60)
        m_undo.removeFirst();
    m_redo.clear();
}

void PixelCanvas::undo()
{
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_img);
    m_img = m_undo.takeLast();
    update();
    emit changed();
}

void PixelCanvas::redo()
{
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_img);
    m_img = m_redo.takeLast();
    update();
    emit changed();
}

void PixelCanvas::paintPixel(const QPoint& p)
{
    if (p.x() < 0 || p.y() < 0 || p.x() > 63 || p.y() > 63)
        return;
    m_img.setPixelColor(p, m_tool == Tool::Eraser ? QColor(0, 0, 0, 0) : m_color);
}

void PixelCanvas::drawLine(QPoint a, QPoint b)
{
    const int dx = qAbs(b.x() - a.x());
    const int dy = -qAbs(b.y() - a.y());
    const int sx = a.x() < b.x() ? 1 : -1;
    const int sy = a.y() < b.y() ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        paintPixel(a);
        if (a == b)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            a.rx() += sx;
        }
        if (e2 <= dx) {
            err += dx;
            a.ry() += sy;
        }
    }
}

void PixelCanvas::fill(const QPoint& start)
{
    QRect reg(0, 0, 64, 64);
    for (const auto& g : regions()) {
        const QRect q(g.x, g.y, g.w, g.h);
        if (q.contains(start)) {
            reg = q;
            break;
        }
    }
    const QColor from = m_img.pixelColor(start);
    if (from == m_color)
        return;
    QList<QPoint> stack{ start };
    while (!stack.isEmpty()) {
        const QPoint p = stack.takeLast();
        if (!reg.contains(p) || m_img.pixelColor(p) != from)
            continue;
        m_img.setPixelColor(p, m_color);
        stack << p + QPoint(1, 0) << p + QPoint(-1, 0) << p + QPoint(0, 1) << p + QPoint(0, -1);
    }
}

void PixelCanvas::pick(const QPoint& p)
{
    const QColor c = m_img.pixelColor(p);
    if (c.alpha() > 0) {
        m_color = c;
        emit colorPicked(c);
    }
}

void PixelCanvas::mousePressEvent(QMouseEvent* e)
{
    const QPoint px = pixelAt(e->position().toPoint());
    if (px.x() < 0)
        return;
    if (e->button() == Qt::RightButton) {  // clic derecho = gotero
        pick(px);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (m_tool == Tool::Picker) {
        pick(px);
        return;
    }
    pushUndo();
    if (m_tool == Tool::Fill) {
        fill(px);
    } else {
        m_drawing = true;
        m_last = px;
        paintPixel(px);
    }
    update();
    emit changed();
}

void PixelCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_drawing)
        return;
    const QPoint px = pixelAt(e->position().toPoint());
    if (px.x() < 0)
        return;
    drawLine(m_last, px);
    m_last = px;
    update();
    emit changed();
}

void PixelCanvas::mouseReleaseEvent(QMouseEvent*)
{
    m_drawing = false;
}

// ----------------------------------------------------------- SkinEditorDialog
SkinEditorDialog::SkinEditorDialog(QWidget* parent, const QImage& skin, bool slim, const QString& name) : QDialog(parent)
{
    setWindowTitle(tr("Editor de skin"));
    resize(1120, 720);

    QImage img = skin.convertToFormat(QImage::Format_ARGB32);
    if (img.size() != QSize(64, 64)) {  // skins antiguas 64x32: se copian a un lienzo 64x64
        QImage big(64, 64, QImage::Format_ARGB32);
        big.fill(Qt::transparent);
        QPainter pp(&big);
        pp.drawImage(0, 0, img);
        pp.end();
        img = big;
    }
    m_tmpPath = m_tmp.filePath("skin.png");
    img.save(m_tmpPath, "PNG");
    m_model = SkinModel(m_tmpPath);
    m_model.setModel(slim ? SkinModel::SLIM : SkinModel::CLASSIC);

    m_canvas = new PixelCanvas(this);
    m_canvas->setImage(img);

    QWidget* previewWidget = nullptr;
    if (SkinOpenGLWindow::hasOpenGL()) {
        m_preview = new SkinOpenGLWindow(this, palette().color(QPalette::Normal, QPalette::Base));
        previewWidget = QWidget::createWindowContainer(m_preview, this);
    } else {
        m_previewLabel = new QLabel(this);
        m_previewLabel->setAlignment(Qt::AlignCenter);
        previewWidget = m_previewLabel;
    }
    previewWidget->setMinimumSize(300, 420);

    // --- herramientas
    auto* tools = new QHBoxLayout();
    auto* group = new QButtonGroup(this);
    const struct {
        const char* text;
        PixelCanvas::Tool tool;
    } defs[] = { { QT_TR_NOOP("Lapiz"), PixelCanvas::Tool::Pencil },
                 { QT_TR_NOOP("Borrador"), PixelCanvas::Tool::Eraser },
                 { QT_TR_NOOP("Relleno"), PixelCanvas::Tool::Fill },
                 { QT_TR_NOOP("Gotero"), PixelCanvas::Tool::Picker } };
    for (const auto& d : defs) {
        auto* b = new QPushButton(tr(d.text), this);
        b->setCheckable(true);
        b->setChecked(d.tool == PixelCanvas::Tool::Pencil);
        group->addButton(b);
        tools->addWidget(b);
        const auto tool = d.tool;
        connect(b, &QPushButton::clicked, this, [this, tool]() { m_canvas->setTool(tool); });
    }
    group->setExclusive(true);

    // --- color
    m_colorBtn = new QPushButton(this);
    m_colorBtn->setMinimumHeight(34);
    connect(m_colorBtn, &QPushButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(m_canvas->property("bloqueColor").value<QColor>(), this, tr("Color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid())
            setColor(c, true);
    });
    connect(m_canvas, &PixelCanvas::colorPicked, this, [this](const QColor& c) { setColor(c, true); });

    // muestras fijas (tonos de piel, pelo, ropa...)
    auto* presets = new QGridLayout();
    const char* pcols[] = { "#f2c9a0", "#d9a273", "#c68c63", "#8d5a3b", "#4a2f1d", "#2b1b12", "#ffffff", "#c0c0c0",
                            "#606060", "#000000", "#e24b4b", "#e8a317", "#f3e04a", "#4fbf5f", "#2fb5b5", "#3b82f6",
                            "#6b4de6", "#d94fb0" };
    int i = 0;
    for (const char* hex : pcols) {
        auto* b = new QPushButton(this);
        b->setFixedSize(24, 24);
        b->setStyleSheet(QString("background:%1;border:1px solid #00000088;border-radius:4px;").arg(hex));
        const QColor c(hex);
        connect(b, &QPushButton::clicked, this, [this, c]() { setColor(c, true); });
        presets->addWidget(b, i / 9, i % 9);
        ++i;
    }
    m_palLayout = new QGridLayout();

    // --- opciones
    auto* grid = new QCheckBox(tr("Cuadricula"), this);
    grid->setChecked(true);
    connect(grid, &QCheckBox::toggled, m_canvas, &PixelCanvas::setShowGrid);
    auto* guide = new QCheckBox(tr("Guia de partes"), this);
    guide->setChecked(true);
    connect(guide, &QCheckBox::toggled, m_canvas, &PixelCanvas::setShowGuide);
    auto* slimCb = new QCheckBox(tr("Brazos delgados (solo vista previa)"), this);
    slimCb->setChecked(slim);
    connect(slimCb, &QCheckBox::toggled, this, [this](bool on) {
        m_model.setModel(on ? SkinModel::SLIM : SkinModel::CLASSIC);
        refreshPreview();
    });
    auto* undoBtn = new QPushButton(tr("Deshacer"), this);
    auto* redoBtn = new QPushButton(tr("Rehacer"), this);
    connect(undoBtn, &QPushButton::clicked, m_canvas, &PixelCanvas::undo);
    connect(redoBtn, &QPushButton::clicked, m_canvas, &PixelCanvas::redo);
    auto* ur = new QHBoxLayout();
    ur->addWidget(undoBtn);
    ur->addWidget(redoBtn);

    m_name = new QLineEdit(name + " editada", this);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Guardar como skin nueva"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* side = new QVBoxLayout();
    side->addLayout(tools);
    side->addWidget(m_colorBtn);
    side->addWidget(new QLabel(tr("Colores fijos"), this));
    side->addLayout(presets);
    side->addWidget(new QLabel(tr("Usados hace poco"), this));
    side->addLayout(m_palLayout);
    side->addWidget(grid);
    side->addWidget(guide);
    side->addWidget(slimCb);
    side->addLayout(ur);
    side->addWidget(new QLabel(tr("Clic derecho = gotero"), this));
    side->addStretch(1);
    side->addWidget(new QLabel(tr("Nombre de la skin"), this));
    side->addWidget(m_name);

    auto* main = new QHBoxLayout();
    main->addWidget(m_canvas, 3);
    main->addWidget(previewWidget, 2);
    auto* sideW = new QWidget(this);
    sideW->setLayout(side);
    sideW->setFixedWidth(260);
    main->addWidget(sideW);

    auto* root = new QVBoxLayout(this);
    root->addLayout(main, 1);
    root->addWidget(buttons);

    m_timer.setSingleShot(true);
    m_timer.setInterval(120);
    connect(&m_timer, &QTimer::timeout, this, &SkinEditorDialog::refreshPreview);
    connect(m_canvas, &PixelCanvas::changed, &m_timer, qOverload<>(&QTimer::start));

    setColor(QColor(0xe8, 0xa3, 0x17), false);
    refreshPreview();
}

SkinEditorDialog::~SkinEditorDialog()
{
    delete m_preview;
}

QImage SkinEditorDialog::image() const
{
    return m_canvas->image();
}

QString SkinEditorDialog::skinName() const
{
    return m_name->text();
}

void SkinEditorDialog::refreshPreview()
{
    m_canvas->image().save(m_tmpPath, "PNG");
    m_model.refresh();
    if (m_preview) {
        m_preview->updateScene(&m_model);
    } else if (m_previewLabel) {
        m_previewLabel->setPixmap(
            QPixmap::fromImage(m_model.getPreview()).scaled(m_previewLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    }
}

void SkinEditorDialog::setColor(const QColor& c, bool addRecent)
{
    m_canvas->setColor(c);
    m_canvas->setProperty("bloqueColor", c);
    m_colorBtn->setStyleSheet(QString("background:rgba(%1,%2,%3,%4);border:2px solid #ffffff55;border-radius:8px;")
                                  .arg(c.red())
                                  .arg(c.green())
                                  .arg(c.blue())
                                  .arg(c.alpha()));
    m_colorBtn->setText(c.name());
    if (addRecent) {
        m_recent.removeAll(c);
        m_recent.prepend(c);
        while (m_recent.size() > 14)
            m_recent.removeLast();
        rebuildPalette();
    }
}

void SkinEditorDialog::rebuildPalette()
{
    while (QLayoutItem* it = m_palLayout->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    int i = 0;
    for (const QColor& c : m_recent) {
        auto* b = new QPushButton(this);
        b->setFixedSize(24, 24);
        b->setStyleSheet(QString("background:%1;border:1px solid #00000088;border-radius:4px;").arg(c.name()));
        connect(b, &QPushButton::clicked, this, [this, c]() { setColor(c, false); });
        m_palLayout->addWidget(b, i / 9, i % 9);
        ++i;
    }
}
