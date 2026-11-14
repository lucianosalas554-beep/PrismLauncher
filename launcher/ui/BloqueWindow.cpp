// SPDX-License-Identifier: GPL-3.0-only
#include "BloqueWindow.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "icons/IconList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/auth/AccountList.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/skins/SkinManageDialog.h"

namespace {

QString describe(MinecraftInstance* inst)
{
    QStringList parts;
    if (auto* pp = inst->getPackProfile()) {
        const QString mc = pp->getComponentVersion("net.minecraft");
        QString loader;
        if (!pp->getComponentVersion("net.neoforged").isEmpty())
            loader = "NeoForge";
        else if (!pp->getComponentVersion("net.minecraftforge").isEmpty())
            loader = "Forge";
        else if (!pp->getComponentVersion("net.fabricmc.fabric-loader").isEmpty())
            loader = "Fabric";
        else if (!pp->getComponentVersion("org.quiltmc.quilt-loader").isEmpty())
            loader = "Quilt";
        if (!loader.isEmpty())
            parts << loader;
        if (!mc.isEmpty())
            parts << mc;
    }
    return parts.join(" - ");
}

QString gradient(int hue)
{
    return QString("qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 %1,stop:1 %2)")
        .arg(QColor::fromHsl(hue, 200, 115).name(), QColor::fromHsl((hue + 55) % 360, 190, 60).name());
}

QPushButton* action(QWidget* parent, const QString& text, bool primary, const std::function<void()>& fn)
{
    auto* b = new QPushButton(text, parent);
    b->setProperty("primary", primary);
    b->setCursor(Qt::PointingHandCursor);
    QObject::connect(b, &QPushButton::clicked, parent, [fn]() { fn(); });
    return b;
}

QScrollArea* wrapScroll(QWidget* inner)
{
    auto* s = new QScrollArea();
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    s->setWidget(inner);
    return s;
}

QLabel* title(const QString& text, const QString& sub = QString())
{
    auto* l = new QLabel(sub.isEmpty() ? QString("<h1>%1</h1>").arg(text) : QString("<h1>%1</h1><p style='color:#8d9bb8'>%2</p>").arg(text, sub));
    l->setWordWrap(true);
    return l;
}

// Tarjeta con franja de color (la inicial grande), titulo, descripcion y botones.
QFrame* card(QWidget* parent, const QString& name, const QString& desc, int hue, const QList<QPushButton*>& buttons)
{
    auto* f = new QFrame(parent);
    f->setObjectName("card");
    auto* v = new QVBoxLayout(f);
    auto* band = new QLabel(name.left(1).toUpper(), f);
    band->setAlignment(Qt::AlignCenter);
    band->setMinimumHeight(90);
    band->setStyleSheet(QString("background:%1;border-radius:12px;color:rgba(255,255,255,.85);font-size:44px;font-weight:800;").arg(gradient(hue)));
    v->addWidget(band);
    auto* t = new QLabel(QString("<b>%1</b>").arg(name), f);
    t->setWordWrap(true);
    v->addWidget(t);
    auto* d = new QLabel(desc, f);
    d->setWordWrap(true);
    d->setStyleSheet("color:#8d9bb8;");
    v->addWidget(d, 1);
    auto* row = new QHBoxLayout();
    for (auto* b : buttons) {
        b->setParent(f);
        row->addWidget(b);
    }
    row->addStretch(1);
    v->addLayout(row);
    return f;
}

void clearLayout(QLayout* l)
{
    while (QLayoutItem* it = l->takeAt(0)) {
        delete it->widget();
        delete it;
    }
}

struct Series {
    const char* name;
    const char* desc;
    const char* query;
    bool pack;
};
const Series kSeries[] = {
    { "Karmaland", "Servidor con mods y reglas propias, junto a otros youtubers.", "karmaland", false },
    { "Minecraft con Noobs", "Una de sus series mas populares.", "minecraft con noobs", false },
    { "Planeta Vegetta", "Su serie mas iconica de aventuras en Minecraft.", "planeta vegetta", true },
    { "Apocalipsis Minecraft", "100 dias de supervivencia junto a Willyrex.", "apocalipsis minecraft 100 dias", true },
    { "V for Samuel", "Serie de Minecraft de su canal.", "v for samuel", false },
    { "Minecraft Zone", "Serie de Minecraft de su canal.", "minecraft zone", false },
    { "Survival en DiosesMC", "Survival en servidor, serie reciente.", "diosesmc survival", false },
    { "LEGO Horizon Adventures", "Gameplay del juego (no es Minecraft).", "lego horizon adventures", false },
};

struct Pack {
    const char* name;
    const char* desc;
};
const Pack kPacks[] = {
    { "Fabulously Optimized", "Mas FPS y shaders listos para usar." },
    { "Cobbleverse", "Cobblemon con aventura, regiones y criaturas." },
    { "Better MC", "Vanilla mejorado: estructuras, mobs y cuevas." },
    { "Adrenaline", "Rendimiento maximo para equipos modestos." },
    { "All the Mods 10", "Cientos de mods de tecnologia y magia." },
    { "RLCraft", "Supervivencia extrema con dragones." },
    { "Prominence II RPG", "Aventura RPG con jefes y mazmorras." },
    { "Vault Hunters", "Mazmorras procedurales y botin infinito." },
};

const char* kStyle = R"(
QMainWindow, #root { background:#0b1322; }
QLabel { color:#eaf0fb; }
QLabel h1 { font-size:26px; }
#side { background:#0f182b; border-right:1px solid rgba(255,255,255,0.07); }
#logo { color:#ffffff; font-size:20px; font-weight:800; padding:6px 10px 14px 10px; }
#sec { color:#8d9bb8; font-size:11px; font-weight:700; padding:14px 12px 4px 12px; }
QPushButton { background:#1c2a45; color:#eaf0fb; border:1px solid rgba(255,255,255,0.08); border-radius:12px; padding:8px 16px; font-weight:600; }
QPushButton:hover { background:#26375a; }
QPushButton[primary="true"] { background:#3b82f6; color:#ffffff; border:0; }
QPushButton[primary="true"]:hover { background:#5b97f8; }
QPushButton[nav="true"] { text-align:left; background:transparent; border:0; color:#8d9bb8; padding:11px 14px; }
QPushButton[nav="true"]:hover { background:rgba(255,255,255,0.06); color:#eaf0fb; }
QPushButton[nav="true"]:checked { background:rgba(59,130,246,0.22); color:#ffffff; }
QLineEdit { background:rgba(255,255,255,0.07); color:#eaf0fb; border:1px solid rgba(255,255,255,0.09); border-radius:14px; padding:10px 14px; }
QListWidget { background:transparent; color:#eaf0fb; border:0; outline:0; }
QListWidget::item { padding:6px; border-radius:10px; }
QListWidget::item:hover { background:rgba(255,255,255,0.06); }
QListWidget::item:selected { background:rgba(59,130,246,0.30); color:#ffffff; }
#card { background:#131e33; border:1px solid rgba(255,255,255,0.07); border-radius:16px; }
#panel { background:#131e33; border:1px solid rgba(255,255,255,0.07); border-radius:16px; }
QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; }
QScrollBar:vertical { background:transparent; width:10px; }
QScrollBar::handle:vertical { background:#2a3d63; border-radius:5px; min-height:30px; }
QScrollBar::add-line, QScrollBar::sub-line { height:0; width:0; }
#chip { padding:6px 16px; }
)";

}  // namespace

// ------------------------------------------------------------- BloqueCarousel
BloqueCarousel::BloqueCarousel(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(300);
    m_play = action(this, tr("Jugar"), true, [this]() { emit playRequested(currentId()); });
    m_edit = action(this, tr("Editar"), false, [this]() { emit editRequested(currentId()); });
    m_prev = action(this, QString::fromLatin1("<"), false, [this]() { goTo(m_cur - 1); });
    m_next = action(this, QString::fromLatin1(">"), false, [this]() { goTo(m_cur + 1); });
    m_prev->setFixedSize(42, 42);
    m_next->setFixedSize(42, 42);
    m_play->setFixedSize(140, 44);
    m_edit->setFixedSize(100, 44);
    m_anim.setDuration(380);
    m_anim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_pos = v.toDouble();
        update();
    });
    connect(&m_anim, &QVariantAnimation::finished, this, [this]() {
        m_play->setVisible(!m_items.isEmpty());
        m_edit->setVisible(!m_items.isEmpty());
        layoutButtons();
    });
}

void BloqueCarousel::setItems(const QList<Item>& items)
{
    const QString keep = currentId();
    m_items = items;
    m_cur = 0;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == keep)
            m_cur = i;
    }
    m_anim.stop();
    m_pos = m_cur;
    m_play->setVisible(!m_items.isEmpty());
    m_edit->setVisible(!m_items.isEmpty());
    m_prev->setVisible(m_items.size() > 1);
    m_next->setVisible(m_items.size() > 1);
    layoutButtons();
    update();
}

void BloqueCarousel::select(const QString& id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id) {
            goTo(i);
            return;
        }
    }
}

QString BloqueCarousel::currentId() const
{
    return (m_cur >= 0 && m_cur < m_items.size()) ? m_items[m_cur].id : QString();
}

void BloqueCarousel::goTo(int i)
{
    if (m_items.isEmpty())
        return;
    i = qBound(0, i, int(m_items.size()) - 1);
    if (i == m_cur)
        return;
    m_cur = i;
    m_play->hide();
    m_edit->hide();
    m_anim.stop();
    m_anim.setStartValue(m_pos);
    m_anim.setEndValue(double(i));
    m_anim.start();
    emit currentChanged(currentId());
}

void BloqueCarousel::resizeEvent(QResizeEvent*)
{
    layoutButtons();
}

void BloqueCarousel::layoutButtons()
{
    const double W = qMin(width() * 0.56, 560.0), H = height() - 24.0, cx = width() / 2.0, cy = height() / 2.0;
    m_play->move(int(cx - W / 2 + 28), int(cy + H / 2 - 74));
    m_edit->move(int(cx - W / 2 + 28 + 150), int(cy + H / 2 - 74));
    m_prev->move(8, height() / 2 - 21);
    m_next->move(width() - 50, height() / 2 - 21);
    m_prev->raise();
    m_next->raise();
}

void BloqueCarousel::mousePressEvent(QMouseEvent* e)
{
    for (int k = m_hit.size() - 1; k >= 0; --k) {
        if (m_hit[k].second.contains(e->position())) {
            goTo(m_hit[k].first);
            return;
        }
    }
}

void BloqueCarousel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    m_hit.clear();
    if (m_items.isEmpty()) {
        p.setPen(QColor("#8d9bb8"));
        p.drawText(rect(), Qt::AlignCenter, tr("Todavia no hay instancias. Crea una desde Descargar."));
        return;
    }
    const double W = qMin(width() * 0.56, 560.0), H = height() - 24.0, cx = width() / 2.0, cy = height() / 2.0;
    QList<int> order;
    for (int i = 0; i < m_items.size(); ++i) {
        if (qAbs(i - m_pos) < 2.6)
            order << i;
    }
    std::sort(order.begin(), order.end(), [this](int a, int b) { return qAbs(a - m_pos) > qAbs(b - m_pos); });
    for (int i : order) {
        const Item& it = m_items[i];
        const double d = i - m_pos, ad = qMin(qAbs(d), 2.0), s = 1.0 - ad * 0.16;
        const double w = W * s, h = H * s;
        const QRectF r(cx + d * W * 0.78 - w / 2, cy - h / 2, w, h);
        m_hit.append({ i, r });
        p.save();
        p.setOpacity(1.0 - ad * 0.28);
        QPainterPath path;
        path.addRoundedRect(r, 22 * s, 22 * s);
        p.setClipPath(path);
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, QColor::fromHsl(it.hue, 200, 115));
        g.setColorAt(1, QColor::fromHsl((it.hue + 55) % 360, 190, 60));
        p.fillRect(r, g);
        QLinearGradient sh(r.topLeft(), r.topRight());
        sh.setColorAt(0, QColor(0, 0, 0, 170));
        sh.setColorAt(0.7, QColor(0, 0, 0, 0));
        p.fillRect(r, sh);
        if (!it.icon.isNull()) {
            const double sz = 88 * s;
            p.drawPixmap(QRectF(r.right() - sz - 22 * s, r.top() + 22 * s, sz, sz), it.icon, QRectF(it.icon.rect()));
        }
        p.setPen(Qt::white);
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(qMax(10, int(30 * s)));
        p.setFont(f);
        p.drawText(QRectF(r.left() + 28 * s, r.bottom() - h * 0.52, w * 0.62, 80 * s), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, it.name);
        f.setBold(false);
        f.setPixelSize(qMax(8, int(14 * s)));
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, 220));
        p.drawText(QRectF(r.left() + 28 * s, r.bottom() - h * 0.52 + 84 * s, w * 0.7, 24 * s), Qt::AlignLeft | Qt::AlignTop, it.sub);
        p.restore();
    }
}

// --------------------------------------------------------------- BloqueWindow
BloqueWindow::BloqueWindow(MainWindow* classicWindow) : m_classic(classicWindow)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Bloque Launcher");
    resize(1280, 800);
    setStyleSheet(kStyle);

    auto* root = new QWidget(this);
    root->setObjectName("root");
    setCentralWidget(root);
    auto* h = new QHBoxLayout(root);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    // ---- barra lateral
    auto* side = new QWidget(root);
    side->setObjectName("side");
    side->setFixedWidth(236);
    auto* sv = new QVBoxLayout(side);
    sv->setContentsMargins(14, 18, 14, 14);
    sv->setSpacing(4);
    auto* logo = new QLabel("BLOQUE", side);
    logo->setObjectName("logo");
    sv->addWidget(logo);

    m_pages = new QStackedWidget(root);
    m_nav = new QButtonGroup(this);
    m_nav->setExclusive(true);
    const QStringList names = { tr("Inicio"), tr("Instancias"), tr("Descargar"), tr("Videos"), tr("Cuenta y skins"), tr("Ajustes") };
    for (int i = 0; i < names.size(); ++i) {
        auto* b = new QPushButton(names[i], side);
        b->setCheckable(true);
        b->setProperty("nav", true);
        b->setCursor(Qt::PointingHandCursor);
        m_nav->addButton(b, i);
        sv->addWidget(b);
    }
    m_nav->button(0)->setChecked(true);
    connect(m_nav, &QButtonGroup::idClicked, this, [this](int id) { m_pages->setCurrentIndex(id); });

    auto* sec = new QLabel(tr("INICIO RAPIDO"), side);
    sec->setObjectName("sec");
    sv->addWidget(sec);
    m_quick = new QListWidget(side);
    m_quick->setIconSize(QSize(28, 28));
    sv->addWidget(m_quick, 1);
    sv->addWidget(action(side, tr("Gestor clasico de Prism"), false, [this]() {
        if (m_classic) {
            m_classic->show();
            m_classic->raise();
        }
    }));
    h->addWidget(side);

    // ---- zona derecha: barra superior + paginas
    auto* right = new QWidget(root);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(26, 18, 26, 18);
    rv->setSpacing(14);
    auto* top = new QHBoxLayout();
    m_search = new QLineEdit(right);
    m_search->setPlaceholderText(tr("Buscar instancias"));
    m_search->setMaximumWidth(520);
    top->addWidget(m_search, 1);
    top->addStretch(1);
    m_chip = action(right, tr("Sin cuenta"), false, [this]() { go(4); });
    m_chip->setObjectName("chip");
    top->addWidget(m_chip);
    rv->addLayout(top);

    m_pages->addWidget(makeHome());
    m_pages->addWidget(makeInstances());
    m_pages->addWidget(makeDownload());
    m_pages->addWidget(makeVideos());
    m_pages->addWidget(makeAccount());
    m_pages->addWidget(makeSettings());
    rv->addWidget(m_pages, 1);
    h->addWidget(right, 1);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) {
        m_filter = t.trimmed();
        refresh();
        if (!m_filter.isEmpty())
            go(1);
    });
    connect(m_quick, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { m_carousel->select(it->data(Qt::UserRole).toString()); });
    connect(m_quick, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { launch(it->data(Qt::UserRole).toString()); });
    connect(m_carousel, &BloqueCarousel::playRequested, this, [this](const QString& id) { launch(id); });
    connect(m_carousel, &BloqueCarousel::editRequested, this, [this](const QString& id) { editInstance(id); });
    connect(APPLICATION->instances(), &InstanceList::instancesChanged, this, [this]() { refresh(); });
    connect(APPLICATION->accounts(), &AccountList::defaultAccountChanged, this, [this]() { updateAccount(); });
    connect(APPLICATION->accounts(), &AccountList::listChanged, this, [this]() { updateAccount(); });

    refresh();
    updateAccount();
}

void BloqueWindow::closeEvent(QCloseEvent* e)
{
    e->accept();
    if (m_classic)
        m_classic->close();  // el cierre de la ventana clasica hace que Prism termine normalmente
}

void BloqueWindow::go(int page)
{
    m_pages->setCurrentIndex(page);
    if (auto* b = m_nav->button(page))
        b->setChecked(true);
}

void BloqueWindow::classic(const char* slot)
{
    if (m_classic)
        QMetaObject::invokeMethod(m_classic, slot);
}

void BloqueWindow::launch(const QString& id)
{
    auto* inst = APPLICATION->instances()->getInstanceById(id);
    if (inst && !inst->isRunning())
        APPLICATION->launch(inst);
}

void BloqueWindow::editInstance(const QString& id)
{
    if (auto* inst = APPLICATION->instances()->getInstanceById(id))
        APPLICATION->showInstanceWindow(inst);
}

QString BloqueWindow::currentInstanceId() const
{
    if (m_grid->currentItem())
        return m_grid->currentItem()->data(Qt::UserRole).toString();
    return m_carousel->currentId();
}

void BloqueWindow::updateAccount()
{
    auto acc = APPLICATION->accounts()->defaultAccount();
    m_chip->setText(acc ? acc->profileName() : tr("Sin cuenta"));
}

void BloqueWindow::openSkins()
{
    auto account = APPLICATION->accounts()->defaultAccount();
    if (account && (account->accountType() == AccountType::MSA) && !account->isActive()) {
        SkinManageDialog dialog(this, account);
        dialog.exec();
    } else {
        CustomMessageBox::selectable(this, tr("Skins"), tr("Inicia sesion con una cuenta de Microsoft para gestionar tus skins."), QMessageBox::Information)
            ->show();
    }
}

void BloqueWindow::refresh()
{
    QList<BloqueCarousel::Item> items;
    m_quick->clear();
    m_grid->clear();
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        auto* inst = list->at(i);
        if (!m_filter.isEmpty() && !inst->name().contains(m_filter, Qt::CaseInsensitive))
            continue;
        const QIcon icon = APPLICATION->icons()->getIcon(inst->iconKey());
        BloqueCarousel::Item it;
        it.id = inst->id();
        it.name = inst->name();
        it.sub = describe(inst);
        it.icon = icon.pixmap(96, 96);
        it.hue = int(qHash(it.id) % 360);
        items << it;
        auto* q = new QListWidgetItem(icon, inst->name(), m_quick);
        q->setData(Qt::UserRole, it.id);
        auto* g = new QListWidgetItem(icon, inst->name(), m_grid);
        g->setData(Qt::UserRole, it.id);
        g->setToolTip(it.sub);
    }
    m_carousel->setItems(items);
}

QWidget* BloqueWindow::makeHome()
{
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(12);
    m_carousel = new BloqueCarousel(w);
    v->addWidget(m_carousel, 3);

    auto* head = new QHBoxLayout();
    head->addWidget(new QLabel(tr("<h2>Modpacks populares</h2>")));
    head->addStretch(1);
    head->addWidget(action(w, tr("Ver todo"), false, [this]() { go(2); }));
    v->addLayout(head);

    auto* row = new QWidget();
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    int k = 0;
    for (const auto& pk : kPacks) {
        auto* c = card(row, pk.name, pk.desc, int((k * 47 + 20) % 360), { action(row, tr("Descargar"), true, [this]() { classic("on_actionAddInstance_triggered"); }) });
        c->setMinimumWidth(230);
        rl->addWidget(c);
        ++k;
    }
    auto* s = wrapScroll(row);
    s->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    s->setMinimumHeight(250);
    v->addWidget(s, 2);
    return w;
}

QWidget* BloqueWindow::makeInstances()
{
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(title(tr("Instancias"), tr("Cada instancia tiene su carpeta, version, loader y mods. Doble clic para jugar.")));
    auto* bar = new QHBoxLayout();
    bar->addWidget(action(w, tr("Jugar"), true, [this]() { launch(currentInstanceId()); }));
    bar->addWidget(action(w, tr("Editar"), false, [this]() { editInstance(currentInstanceId()); }));
    bar->addWidget(action(w, tr("Abrir carpeta"), false, [this]() {
        if (auto* inst = APPLICATION->instances()->getInstanceById(currentInstanceId()))
            QDesktopServices::openUrl(QUrl::fromLocalFile(inst->instanceRoot()));
    }));
    bar->addWidget(action(w, tr("Nueva instancia"), false, [this]() { classic("on_actionAddInstance_triggered"); }));
    bar->addStretch(1);
    v->addLayout(bar);
    m_grid = new QListWidget(w);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setIconSize(QSize(64, 64));
    m_grid->setGridSize(QSize(150, 120));
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setWordWrap(true);
    connect(m_grid, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { launch(it->data(Qt::UserRole).toString()); });
    v->addWidget(m_grid, 1);
    return w;
}

QWidget* BloqueWindow::makeDownload()
{
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->addWidget(title(tr("Descargar"), tr("Modpacks, importar archivos y mods. Usa los catalogos de Modrinth y CurseForge de Prism.")));
    auto* g = new QGridLayout();
    g->addWidget(card(w, tr("Modpacks"), tr("Busca e instala modpacks de Modrinth y CurseForge como instancia nueva."), 140,
                      { action(w, tr("Buscar modpacks"), true, [this]() { classic("on_actionAddInstance_triggered"); }) }), 0, 0);
    g->addWidget(card(w, tr("Importar .mrpack o .zip"), tr("Importa un modpack que ya tengas descargado (Modrinth .mrpack o CurseForge .zip)."), 200,
                      { action(w, tr("Importar archivo"), true, [this]() { classic("on_actionAddInstance_triggered"); }) }), 0, 1);
    g->addWidget(card(w, tr("Mods, texturas y shaders"), tr("Abre una instancia con Editar y entra a sus paginas de Mods, Resource packs y Shader packs."), 30,
                      { action(w, tr("Ir a Instancias"), false, [this]() { go(1); }) }), 0, 2);
    v->addLayout(g);
    v->addStretch(1);
    return wrapScroll(w);
}

QWidget* BloqueWindow::makeVideos()
{
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->addWidget(title(tr("Videos y series"), tr("Series de Vegetta777 en YouTube. Es una lista parcial; el boton abre la busqueda en tu navegador.")));
    auto* head = new QFrame(w);
    head->setObjectName("panel");
    auto* hl = new QHBoxLayout(head);
    hl->addWidget(new QLabel(tr("<b style='font-size:18px'>VEGETTA777</b><br><span style='color:#8d9bb8'>Canal en YouTube</span>")), 1);
    hl->addWidget(action(head, tr("Ver todas las listas"), true, []() { QDesktopServices::openUrl(QUrl("https://www.youtube.com/@VEGETTA777/playlists")); }));
    hl->addWidget(action(head, tr("Ir al canal"), false, []() { QDesktopServices::openUrl(QUrl("https://www.youtube.com/@VEGETTA777")); }));
    v->addWidget(head);
    auto* q = new QLineEdit(w);
    q->setPlaceholderText(tr("Buscar serie..."));
    q->setMaximumWidth(360);
    v->addWidget(q);
    auto* gw = new QWidget(w);
    auto* grid = new QGridLayout(gw);
    grid->setContentsMargins(0, 0, 0, 0);
    v->addWidget(gw);
    v->addStretch(1);
    auto fill = [grid, gw](const QString& text) {
        clearLayout(grid);
        int n = 0;
        for (const auto& s : kSeries) {
            const QString name = QString::fromUtf8(s.name);
            if (!text.isEmpty() && !name.contains(text, Qt::CaseInsensitive))
                continue;
            const QString query = QString::fromUtf8(s.query);
            QList<QPushButton*> btns;
            btns << action(gw, QObject::tr("Ver en YouTube"), true, [query]() {
                QDesktopServices::openUrl(QUrl("https://www.youtube.com/results?search_query=" + QString::fromLatin1(QUrl::toPercentEncoding("vegetta777 " + query))));
            });
            if (s.pack)
                btns << action(gw, QObject::tr("Modpack no oficial"), false, [query]() {
                    QDesktopServices::openUrl(QUrl("https://www.curseforge.com/minecraft/search?class=modpacks&search=" + QString::fromLatin1(QUrl::toPercentEncoding(query))));
                });
            grid->addWidget(card(gw, name, QString::fromUtf8(s.desc), int((name.size() * 53) % 360), btns), n / 3, n % 3);
            ++n;
        }
    };
    fill(QString());
    connect(q, &QLineEdit::textChanged, w, [fill](const QString& t) { fill(t.trimmed()); });
    return wrapScroll(w);
}

QWidget* BloqueWindow::makeAccount()
{
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(title(tr("Cuenta y skins"), tr("Tu cuenta de Microsoft y tus skins. El editor de skin esta dentro de Editar skins.")));
    auto* row = new QHBoxLayout();
    row->addWidget(action(w, tr("Editar skins y capas"), true, [this]() { openSkins(); }));
    row->addWidget(action(w, tr("Administrar cuentas"), false, [this]() { APPLICATION->ShowGlobalSettings(this, "accounts"); }));
    row->addWidget(action(w, tr("Abrir carpeta de skins"), false, [this]() { classic("on_actionViewSkinsFolder_triggered"); }));
    row->addStretch(1);
    v->addLayout(row);
    v->addStretch(1);
    return w;
}

QWidget* BloqueWindow::makeSettings()
{
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(title(tr("Ajustes"), tr("Apariencia y temas, Java y Minecraft.")));
    auto* row = new QHBoxLayout();
    row->addWidget(action(w, tr("Apariencia y temas"), true, [this]() { APPLICATION->ShowGlobalSettings(this, "appearance-settings"); }));
    row->addWidget(action(w, tr("Java"), false, [this]() { APPLICATION->ShowGlobalSettings(this, "java-settings"); }));
    row->addWidget(action(w, tr("Minecraft"), false, [this]() { APPLICATION->ShowGlobalSettings(this, "minecraft-settings"); }));
    row->addWidget(action(w, tr("Todos los ajustes"), false, [this]() { classic("on_actionSettings_triggered"); }));
    row->addStretch(1);
    v->addLayout(row);
    v->addStretch(1);
    return w;
}
