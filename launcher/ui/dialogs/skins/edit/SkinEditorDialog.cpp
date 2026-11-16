// SPDX-License-Identifier: GPL-3.0-only
#include "SkinEditorDialog.h"

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
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <functional>

#include "ui/SagiriCore.h"

namespace {
struct Region {
    int x, y, w, h;
    const char* name;
};
// Zonas de la textura 64x32/64x64 (formato moderno). El relleno se limita a la zona donde haces clic.
const Region kRegions[12] = {
    { 0, 0, 32, 16, "Cabeza" },    { 32, 0, 32, 16, "Sombrero" }, { 0, 16, 16, 16, "Pierna D" },  { 16, 16, 24, 16, "Torso" },
    { 40, 16, 16, 16, "Brazo D" }, { 0, 32, 16, 16, "Pantalon D" }, { 16, 32, 24, 16, "Chaqueta" }, { 40, 32, 16, 16, "Manga D" },
    { 16, 48, 16, 16, "Pierna I" }, { 32, 48, 16, 16, "Brazo I" },  { 0, 48, 16, 16, "Pantalon I" }, { 48, 48, 16, 16, "Manga I" },
};
}  // namespace

// ---------------------------------------------------------------- PixelCanvas
PixelCanvas::PixelCanvas(QWidget* parent) : QWidget(parent)
{
    m_img = QImage(64, 64, QImage::Format_ARGB32);
    m_img.fill(Qt::transparent);
    setCursor(Qt::CrossCursor);
    setZoom(8);
}

void PixelCanvas::setZoom(int z)
{
    m_zoom = qBound(3, z, 16);
    setFixedSize(64 * m_zoom, 64 * m_zoom);
    update();
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

void PixelCanvas::setRegionVisible(int idx, bool v)
{
    if (idx >= 0 && idx < 12)
        m_hidden[idx] = !v;
    update();
    emit changed();
}

int PixelCanvas::regionCount()
{
    return 12;
}

QString PixelCanvas::regionName(int idx)
{
    return (idx >= 0 && idx < 12) ? QString::fromLatin1(kRegions[idx].name) : QString();
}

QImage PixelCanvas::visibleImage() const
{
    QImage out = m_img;
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    for (int i = 0; i < 12; ++i) {
        if (m_hidden[i])
            p.fillRect(QRect(kRegions[i].x, kRegions[i].y, kRegions[i].w, kRegions[i].h), Qt::transparent);
    }
    p.end();
    return out;
}

int PixelCanvas::regionAt(const QPoint& pt) const
{
    for (int i = 0; i < 12; ++i) {
        if (QRect(kRegions[i].x, kRegions[i].y, kRegions[i].w, kRegions[i].h).contains(pt))
            return i;
    }
    return -1;
}

QPoint PixelCanvas::pixelAt(const QPoint& pos) const
{
    if (!rect().contains(pos))
        return QPoint(-1, -1);
    return QPoint(pos.x() / m_zoom, pos.y() / m_zoom);
}

void PixelCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const int cell = qMax(4, m_zoom * 2);
    for (int y = 0; y < height(); y += cell) {
        for (int x = 0; x < width(); x += cell) {
            const bool odd = ((x / cell) + (y / cell)) % 2;
            p.fillRect(QRect(x, y, cell, cell), odd ? QColor(0x38, 0x38, 0x38) : QColor(0x4a, 0x4a, 0x4a));
        }
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(rect(), m_img);
    for (int i = 0; i < 12; ++i) {
        if (m_hidden[i])
            p.fillRect(QRect(kRegions[i].x * m_zoom, kRegions[i].y * m_zoom, kRegions[i].w * m_zoom, kRegions[i].h * m_zoom), QColor(0, 0, 0, 175));
    }
    if (m_grid && m_zoom >= 6) {
        p.setPen(QColor(255, 255, 255, 34));
        for (int i = 0; i <= 64; ++i) {
            p.drawLine(i * m_zoom, 0, i * m_zoom, height());
            p.drawLine(0, i * m_zoom, width(), i * m_zoom);
        }
    }
    if (m_guide) {
        QFont f = font();
        f.setPixelSize(10);
        p.setFont(f);
        for (int i = 0; i < 12; ++i) {
            const QRect rr(kRegions[i].x * m_zoom, kRegions[i].y * m_zoom, kRegions[i].w * m_zoom, kRegions[i].h * m_zoom);
            p.setPen(QColor(255, 200, 60, 210));
            p.drawRect(rr);
            p.setPen(QColor(255, 215, 90));
            p.drawText(rr.adjusted(3, 1, 0, 0), Qt::AlignLeft | Qt::AlignTop, tr(kRegions[i].name));
        }
    }
}

void PixelCanvas::pushUndo()
{
    m_undo.append(m_img);
    if (m_undo.size() > 80)
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
    for (int dy = 0; dy < m_brush; ++dy) {
        for (int dx = 0; dx < m_brush; ++dx) {
            const QPoint q(p.x() + dx - m_brush / 2, p.y() + dy - m_brush / 2);
            if (q.x() < 0 || q.y() < 0 || q.x() > 63 || q.y() > 63)
                continue;
            const int r = regionAt(q);
            if (r >= 0 && m_hidden[r])
                continue;  // no se pinta en capas ocultas
            m_img.setPixelColor(q, m_tool == Tool::Eraser ? QColor(0, 0, 0, 0) : m_color);
        }
    }
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
    const int ri = regionAt(start);
    if (ri >= 0) {
        if (m_hidden[ri])
            return;
        reg = QRect(kRegions[ri].x, kRegions[ri].y, kRegions[ri].w, kRegions[ri].h);
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

void PixelCanvas::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        emit zoomWheel(e->angleDelta().y() > 0 ? 1 : -1);
        e->accept();
    } else {
        e->ignore();
    }
}

// ----------------------------------------------------------- SkinEditorDialog
SkinEditorDialog::SkinEditorDialog(QWidget* parent, const QImage& skin, bool slim, const QString& name) : QDialog(parent)
{
    setWindowTitle(tr("Editor de skin"));
    resize(1320, 800);

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

    // ---- barra de herramientas (izquierda)
    auto* toolbar = new QVBoxLayout();
    toolbar->setSpacing(6);
    const struct {
        const char* icon;
        const char* tip;
    } defs[4] = { { "edit", "Lapiz (B)" }, { "eraser", "Borrador (E)" }, { "bucket", "Relleno (G)" }, { "picker", "Gotero (I)" } };
    for (int i = 0; i < 4; ++i) {
        auto* b = new QToolButton(this);
        b->setIcon(Sagiri::icon(defs[i].icon, Qt::white, 24));
        b->setIconSize(QSize(24, 24));
        b->setToolTip(tr(defs[i].tip));
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFixedSize(46, 46);
        b->setChecked(i == 0);
        connect(b, &QToolButton::clicked, this, [this, i]() { setTool(i); });
        m_toolBtns[i] = b;
        toolbar->addWidget(b);
    }
    toolbar->addSpacing(10);
    auto* undoBtn = new QToolButton(this);
    undoBtn->setIcon(Sagiri::icon("undo", Qt::white, 24));
    undoBtn->setToolTip(tr("Deshacer (Ctrl+Z)"));
    undoBtn->setFixedSize(46, 46);
    connect(undoBtn, &QToolButton::clicked, m_canvas, &PixelCanvas::undo);
    auto* redoBtn = new QToolButton(this);
    redoBtn->setIcon(Sagiri::icon("redo", Qt::white, 24));
    redoBtn->setToolTip(tr("Rehacer (Ctrl+Y)"));
    redoBtn->setFixedSize(46, 46);
    connect(redoBtn, &QToolButton::clicked, m_canvas, &PixelCanvas::redo);
    toolbar->addWidget(undoBtn);
    toolbar->addWidget(redoBtn);
    toolbar->addStretch(1);

    // ---- vista 3D (centro)
    QWidget* previewWidget = nullptr;
    if (SkinOpenGLWindow::hasOpenGL()) {
        m_preview = new SkinOpenGLWindow(this, palette().color(QPalette::Normal, QPalette::Base));
        previewWidget = QWidget::createWindowContainer(m_preview, this);
    } else {
        m_previewLabel = new QLabel(this);
        m_previewLabel->setAlignment(Qt::AlignCenter);
        previewWidget = m_previewLabel;
    }
    previewWidget->setMinimumSize(380, 480);
    auto* center = new QVBoxLayout();
    center->addWidget(new QLabel(tr("<b>Vista 3D</b> (arrastra para girar)"), this));
    center->addWidget(previewWidget, 1);
    auto* slimCb = new QCheckBox(tr("Brazos delgados (Alex)"), this);
    slimCb->setChecked(slim);
    connect(slimCb, &QCheckBox::toggled, this, [this](bool on) {
        m_model.setModel(on ? SkinModel::SLIM : SkinModel::CLASSIC);
        refreshPreview();
    });
    center->addWidget(slimCb);

    // ---- panel derecho: textura, color, pincel y capas
    auto* right = new QVBoxLayout();
    right->addWidget(new QLabel(tr("<b>Textura</b> (Ctrl + rueda = zoom, clic derecho = gotero)"), this));
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(m_canvas);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setMinimumSize(420, 420);
    right->addWidget(scroll, 1);
    auto* zoomRow = new QHBoxLayout();
    auto* zoom = new QSlider(Qt::Horizontal, this);
    zoom->setRange(3, 16);
    zoom->setValue(8);
    connect(zoom, &QSlider::valueChanged, m_canvas, &PixelCanvas::setZoom);
    connect(m_canvas, &PixelCanvas::zoomWheel, this, [zoom](int d) { zoom->setValue(zoom->value() + d); });
    zoomRow->addWidget(new QLabel(tr("Zoom"), this));
    zoomRow->addWidget(zoom, 1);
    auto* grid = new QCheckBox(tr("Cuadricula"), this);
    grid->setChecked(true);
    connect(grid, &QCheckBox::toggled, m_canvas, &PixelCanvas::setShowGrid);
    auto* guide = new QCheckBox(tr("Guia"), this);
    guide->setChecked(true);
    connect(guide, &QCheckBox::toggled, m_canvas, &PixelCanvas::setShowGuide);
    zoomRow->addWidget(grid);
    zoomRow->addWidget(guide);
    right->addLayout(zoomRow);

    // color
    auto* colorRow = new QHBoxLayout();
    m_colorBtn = new QPushButton(this);
    m_colorBtn->setFixedSize(54, 34);
    connect(m_colorBtn, &QPushButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(m_canvas->property("sagiriColor").value<QColor>(), this, tr("Color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid())
            setColor(c, true);
    });
    m_hex = new QLineEdit(this);
    m_hex->setMaximumWidth(110);
    connect(m_hex, &QLineEdit::editingFinished, this, [this]() {
        const QColor c(m_hex->text().trimmed());
        if (c.isValid())
            setColor(c, true);
    });
    connect(m_canvas, &PixelCanvas::colorPicked, this, [this](const QColor& c) { setColor(c, true); });
    auto* brush = new QSpinBox(this);
    brush->setRange(1, 6);
    brush->setPrefix(tr("Pincel "));
    connect(brush, &QSpinBox::valueChanged, m_canvas, &PixelCanvas::setBrush);
    colorRow->addWidget(m_colorBtn);
    colorRow->addWidget(m_hex);
    colorRow->addWidget(brush);
    colorRow->addStretch(1);
    right->addLayout(colorRow);

    auto* presets = new QGridLayout();
    presets->setSpacing(4);
    const char* pcols[] = { "#f2c9a0", "#d9a273", "#c68c63", "#8d5a3b", "#4a2f1d", "#2b1b12", "#ffffff", "#c0c0c0", "#606060", "#000000",
                            "#e24b4b", "#e8a317", "#f3e04a", "#4fbf5f", "#2fb5b5", "#3b82f6", "#6b4de6", "#d94fb0" };
    int pi = 0;
    for (const char* hex : pcols) {
        auto* b = new QPushButton(this);
        b->setFixedSize(24, 24);
        b->setStyleSheet(QString("background:%1;border:1px solid #00000088;border-radius:4px;padding:0;").arg(hex));
        const QColor c(hex);
        connect(b, &QPushButton::clicked, this, [this, c]() { setColor(c, true); });
        presets->addWidget(b, pi / 9, pi % 9);
        ++pi;
    }
    right->addLayout(presets);
    m_palLayout = new QGridLayout();
    m_palLayout->setSpacing(4);
    right->addWidget(new QLabel(tr("Usados hace poco"), this));
    right->addLayout(m_palLayout);

    // capas
    right->addWidget(new QLabel(tr("<b>Capas</b> (ocultar parte del cuerpo)"), this));
    auto* layers = new QGridLayout();
    for (int i = 0; i < PixelCanvas::regionCount(); ++i) {
        auto* cb = new QCheckBox(tr(PixelCanvas::regionName(i).toLatin1().constData()), this);
        cb->setChecked(true);
        connect(cb, &QCheckBox::toggled, this, [this, i](bool on) { m_canvas->setRegionVisible(i, on); });
        layers->addWidget(cb, i / 3, i % 3);
    }
    right->addLayout(layers);

    m_name = new QLineEdit(name + " editada", this);
    right->addWidget(new QLabel(tr("Nombre de la skin"), this));
    right->addWidget(m_name);

    auto* body = new QHBoxLayout();
    body->addLayout(toolbar);
    body->addLayout(center, 3);
    auto* rightW = new QWidget(this);
    rightW->setLayout(right);
    rightW->setFixedWidth(460);
    body->addWidget(rightW);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Guardar como skin nueva"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* root = new QVBoxLayout(this);
    root->addLayout(body, 1);
    root->addWidget(buttons);

    // atajos de teclado
    auto shortcut = [this](const char* keys, const std::function<void()>& fn) {
        auto* s = new QShortcut(QKeySequence(QString::fromLatin1(keys)), this);
        connect(s, &QShortcut::activated, this, [fn]() { fn(); });
    };
    shortcut("B", [this]() { setTool(0); });
    shortcut("E", [this]() { setTool(1); });
    shortcut("G", [this]() { setTool(2); });
    shortcut("I", [this]() { setTool(3); });
    shortcut("Ctrl+Z", [this]() { m_canvas->undo(); });
    shortcut("Ctrl+Y", [this]() { m_canvas->redo(); });
    shortcut("Ctrl+Shift+Z", [this]() { m_canvas->redo(); });
    shortcut("]", [brush]() { brush->setValue(brush->value() + 1); });
    shortcut("[", [brush]() { brush->setValue(brush->value() - 1); });

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

void SkinEditorDialog::setTool(int index)
{
    static const PixelCanvas::Tool tools[4] = { PixelCanvas::Tool::Pencil, PixelCanvas::Tool::Eraser, PixelCanvas::Tool::Fill, PixelCanvas::Tool::Picker };
    if (index < 0 || index > 3)
        return;
    m_canvas->setTool(tools[index]);
    m_toolBtns[index]->setChecked(true);
}

void SkinEditorDialog::refreshPreview()
{
    m_canvas->visibleImage().save(m_tmpPath, "PNG");
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
    m_canvas->setProperty("sagiriColor", c);
    m_colorBtn->setStyleSheet(QString("background:rgba(%1,%2,%3,%4);border:2px solid #ffffff66;border-radius:8px;")
                                  .arg(c.red())
                                  .arg(c.green())
                                  .arg(c.blue())
                                  .arg(c.alpha()));
    m_hex->setText(c.name());
    if (addRecent) {
        m_recent.removeAll(c);
        m_recent.prepend(c);
        while (m_recent.size() > 18)
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
        b->setStyleSheet(QString("background:%1;border:1px solid #00000088;border-radius:4px;padding:0;").arg(c.name()));
        connect(b, &QPushButton::clicked, this, [this, c]() { setColor(c, false); });
        m_palLayout->addWidget(b, i / 9, i % 9);
        ++i;
    }
}
