// SPDX-License-Identifier: GPL-3.0-only
#include "BloqueWindow.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <algorithm>
#include <functional>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "SagiriCore.h"
#include "icons/IconList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/auth/AccountList.h"
#include "settings/SettingsObject.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/IconPickerDialog.h"
#include "ui/dialogs/MSALoginDialog.h"
#include "ui/dialogs/skins/SkinManageDialog.h"

namespace {

using Sagiri::Http;

QString loaderName(PackProfile* pp)
{
    if (!pp)
        return QString();
    if (!pp->getComponentVersion("net.neoforged").isEmpty())
        return "NeoForge";
    if (!pp->getComponentVersion("net.minecraftforge").isEmpty())
        return "Forge";
    if (!pp->getComponentVersion("net.fabricmc.fabric-loader").isEmpty())
        return "Fabric";
    if (!pp->getComponentVersion("org.quiltmc.quilt-loader").isEmpty())
        return "Quilt";
    return QString();
}

QString describe(MinecraftInstance* inst)
{
    QStringList parts;
    if (auto* pp = inst->getPackProfile()) {
        const QString loader = loaderName(pp), mc = pp->getComponentVersion("net.minecraft");
        if (!loader.isEmpty())
            parts << loader;
        if (!mc.isEmpty())
            parts << mc;
    }
    return parts.join(" - ");
}

QPushButton* action(QWidget* parent, const QString& text, bool primary, const std::function<void()>& fn, const QString& iconName = QString())
{
    auto* b = new QPushButton(text, parent);
    b->setProperty("primary", primary);
    b->setCursor(Qt::PointingHandCursor);
    if (!iconName.isEmpty()) {
        b->setIcon(Sagiri::icon(iconName, Qt::white, 18));
        b->setIconSize(QSize(18, 18));
    }
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
    auto* l = new QLabel(sub.isEmpty() ? QString("<h1>%1</h1>").arg(text) : QString("<h1>%1</h1><p style='color:#9aa7c7'>%2</p>").arg(text, sub));
    l->setWordWrap(true);
    return l;
}

void clearLayout(QLayout* l)
{
    while (QLayoutItem* it = l->takeAt(0)) {
        delete it->widget();
        delete it;
    }
}

QString shortNum(qint64 n)
{
    if (n >= 1000000)
        return QString::number(n / 1000000.0, 'f', 1) + "M";
    if (n >= 1000)
        return QString::number(n / 1000.0, 'f', 1) + "k";
    return QString::number(n);
}

QString clip(const QString& s, int n)
{
    return s.size() > n ? s.left(n - 3) + "..." : s;
}

// Tarjeta de proyecto de Modrinth: icono real, titulo, autor, descripcion y boton.
QFrame* projectCard(QWidget* parent, const QJsonObject& h, const QString& btnText, const std::function<void()>& fn)
{
    auto* f = new QFrame(parent);
    f->setObjectName("card");
    auto* v = new QVBoxLayout(f);
    v->setSpacing(8);
    auto* top = new QHBoxLayout();
    auto* ic = new QLabel(f);
    ic->setFixedSize(72, 72);
    ic->setAlignment(Qt::AlignCenter);
    ic->setPixmap(Sagiri::icon("box", QColor(255, 255, 255, 150), 40).pixmap(40, 40));
    const QString iconUrl = h.value("icon_url").toString();
    if (!iconUrl.isEmpty())
        Http::get().pixmap(QUrl(iconUrl), ic, [ic](const QPixmap& p) { ic->setPixmap(Sagiri::roundedPixmap(p, 72, 72, 14)); });
    top->addWidget(ic);
    auto* tv = new QVBoxLayout();
    auto* t = new QLabel(QString("<b>%1</b>").arg(h.value("title").toString().toHtmlEscaped()), f);
    t->setWordWrap(true);
    auto* a = new QLabel(QObject::tr("por %1").arg(h.value("author").toString()), f);
    a->setStyleSheet("color:#9aa7c7;font-size:11px;");
    tv->addWidget(t);
    tv->addWidget(a);
    tv->addStretch(1);
    top->addLayout(tv, 1);
    v->addLayout(top);
    auto* d = new QLabel(clip(h.value("description").toString(), 110), f);
    d->setWordWrap(true);
    d->setStyleSheet("color:#9aa7c7;");
    d->setMinimumHeight(44);
    v->addWidget(d, 1);
    auto* row = new QHBoxLayout();
    auto* dl = new QLabel(QObject::tr("%1 descargas").arg(shortNum(qint64(h.value("downloads").toDouble()))), f);
    dl->setStyleSheet("color:#9aa7c7;font-size:11px;");
    row->addWidget(dl);
    row->addStretch(1);
    row->addWidget(action(f, btnText, true, fn, "download"));
    v->addLayout(row);
    return f;
}

// Tarjeta con miniatura (videos y noticias): al hacer clic abre el enlace.
Sagiri::LinkCard* mediaCard(QWidget* parent, const QString& titleText, const QString& sub, const QUrl& thumb, const QUrl& link)
{
    auto* c = new Sagiri::LinkCard(parent);
    c->setObjectName("card");
    c->setFixedWidth(300);
    auto* v = new QVBoxLayout(c);
    v->setContentsMargins(10, 10, 10, 12);
    v->setSpacing(8);
    auto* img = new QLabel(c);
    img->setFixedSize(280, 158);
    img->setAlignment(Qt::AlignCenter);
    img->setStyleSheet("background:rgba(255,255,255,0.06);border-radius:12px;");
    img->setPixmap(Sagiri::icon("image", QColor(255, 255, 255, 120), 40).pixmap(40, 40));
    if (thumb.isValid())
        Http::get().pixmap(thumb, img, [img](const QPixmap& p) { img->setPixmap(Sagiri::roundedPixmap(p, 280, 158, 12)); });
    v->addWidget(img);
    auto* t = new QLabel(QString("<b>%1</b>").arg(clip(titleText, 90).toHtmlEscaped()), c);
    t->setWordWrap(true);
    v->addWidget(t);
    auto* s = new QLabel(sub, c);
    s->setStyleSheet("color:#9aa7c7;font-size:11px;");
    v->addWidget(s);
    QObject::connect(c, &Sagiri::LinkCard::clicked, c, [link]() { QDesktopServices::openUrl(link); });
    return c;
}

struct Vid {
    QString id, title, published;
};

QList<Vid> parseFeed(const QByteArray& xml)
{
    QList<Vid> out;
    QXmlStreamReader r(xml);
    Vid cur;
    bool inEntry = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const auto n = r.qualifiedName();
            if (n == QLatin1String("entry")) {
                inEntry = true;
                cur = Vid();
            } else if (inEntry && n == QLatin1String("yt:videoId")) {
                cur.id = r.readElementText();
            } else if (inEntry && n == QLatin1String("title")) {
                cur.title = r.readElementText();
            } else if (inEntry && n == QLatin1String("published")) {
                cur.published = r.readElementText();
            }
        } else if (r.isEndElement() && r.qualifiedName() == QLatin1String("entry")) {
            if (!cur.id.isEmpty())
                out << cur;
            inEntry = false;
        }
    }
    return out;
}

struct Feed {
    const char* rss;
    const char* channel;
    const char* search;
};
const Feed kFeeds[2] = {
    { "https://www.youtube.com/feeds/videos.xml?channel_id=UCam8T03EOFBsNdR0thrFHdQ", "https://www.youtube.com/@VEGETTA777", "vegetta777" },
    { "https://www.youtube.com/feeds/videos.xml?user=KillerCreeper55", "https://www.youtube.com/@KillerCreeper55", "pacheco cara floja" },
};

// Fondo de la ventana: color, degradado de cristal o imagen propia.
class Backdrop : public QWidget {
   public:
    explicit Backdrop(QWidget* parent) : QWidget(parent) {}

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const auto& cfg = Sagiri::Config::get();
        const Sagiri::Theme t = cfg.theme();
        const QString path = cfg.value("bg_path").toString();
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            const QString key = path + QString::number(width()) + "x" + QString::number(height()) + (t.glass ? "g" : "n");
            if (key != m_key) {
                m_key = key;
                const QPixmap src(path);
                QPixmap s = src.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                s = s.copy((s.width() - width()) / 2, (s.height() - height()) / 2, width(), height());
                QImage img = s.toImage();
                if (t.glass) {
                    const QImage small = img.scaled(qMax(1, img.width() / 20), qMax(1, img.height() / 20), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    img = small.scaled(img.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                }
                m_pm = QPixmap::fromImage(img);
            }
            p.drawPixmap(0, 0, m_pm);
            p.fillRect(rect(), QColor(0, 0, 0, t.glass ? 30 : 120));
            return;
        }
        if (t.glass) {
            QLinearGradient g(0, 0, width(), height());
            g.setColorAt(0, QColor("#6a5cff"));
            g.setColorAt(0.5, QColor("#2b6cff"));
            g.setColorAt(1, QColor("#12c2e9"));
            p.fillRect(rect(), g);
            QRadialGradient r1(width() * 0.15, height() * 0.2, width() * 0.45);
            r1.setColorAt(0, QColor(255, 120, 220, 150));
            r1.setColorAt(1, QColor(255, 120, 220, 0));
            p.fillRect(rect(), r1);
            QRadialGradient r2(width() * 0.85, height() * 0.85, width() * 0.4);
            r2.setColorAt(0, QColor(80, 255, 200, 130));
            r2.setColorAt(1, QColor(80, 255, 200, 0));
            p.fillRect(rect(), r2);
        } else if (t.id == "steam") {
            QLinearGradient g(0, 0, 0, height());
            g.setColorAt(0, QColor("#2a475e"));
            g.setColorAt(0.4, QColor("#1b2838"));
            g.setColorAt(1, QColor("#0e141b"));
            p.fillRect(rect(), g);
        } else {
            p.fillRect(rect(), t.bg);
        }
    }

   private:
    QString m_key;
    QPixmap m_pm;
};

}  // namespace

// ------------------------------------------------------------- BloqueCarousel
BloqueCarousel::BloqueCarousel(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(300);
    m_play = action(this, tr("Jugar"), true, [this]() { emit playRequested(currentId()); }, "play");
    m_edit = action(this, tr("Editar"), false, [this]() { emit editRequested(currentId()); }, "edit");
    m_prev = action(this, QString(), false, [this]() { goTo(m_cur - 1); }, "chevl");
    m_next = action(this, QString(), false, [this]() { goTo(m_cur + 1); }, "chevr");
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
            const double sz = 112 * s;
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
    setWindowTitle("SagiriPrism");
    resize(1320, 820);

    auto* root = new Backdrop(this);
    root->setObjectName("root");
    setCentralWidget(root);
    auto* h = new QHBoxLayout(root);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    // ---- barra lateral
    auto* side = new QWidget(root);
    side->setObjectName("side");
    side->setAttribute(Qt::WA_StyledBackground, true);
    side->setFixedWidth(236);
    auto* sv = new QVBoxLayout(side);
    sv->setContentsMargins(14, 18, 14, 14);
    sv->setSpacing(4);
    m_logo = new QLabel("SagiriPrism", side);
    m_logo->setObjectName("logo");
    sv->addWidget(m_logo);

    m_pages = new QStackedWidget(root);
    m_nav = new QButtonGroup(this);
    m_nav->setExclusive(true);
    const struct {
        const char* name;
        const char* icon;
    } navs[] = { { "Inicio", "home" }, { "Instancias", "box" }, { "Explorar", "store" }, { "Videos", "video" }, { "Cuenta", "user" }, { "Ajustes", "gear" } };
    int idx = 0;
    for (const auto& n : navs) {
        auto* b = new QPushButton(tr(n.name), side);
        b->setCheckable(true);
        b->setProperty("nav", true);
        b->setCursor(Qt::PointingHandCursor);
        b->setIcon(Sagiri::icon(n.icon, Qt::white, 20));
        b->setIconSize(QSize(20, 20));
        m_nav->addButton(b, idx++);
        sv->addWidget(b);
    }
    m_nav->button(0)->setChecked(true);
    connect(m_nav, &QButtonGroup::idClicked, this, [this](int id) { go(id); });

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

    // ---- zona derecha
    auto* right = new QWidget(root);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(26, 18, 26, 18);
    rv->setSpacing(14);
    auto* top = new QHBoxLayout();
    m_search = new QLineEdit(right);
    m_search->setPlaceholderText(tr("Buscar instancias"));
    m_search->setMaximumWidth(520);
    m_search->addAction(Sagiri::icon("search", Qt::white, 18), QLineEdit::LeadingPosition);
    top->addWidget(m_search, 1);
    top->addStretch(1);
    m_chip = action(right, tr("Sin cuenta"), false, [this]() { go(4); });
    m_chip->setIconSize(QSize(28, 28));
    top->addWidget(m_chip);
    rv->addLayout(top);

    m_pages->addWidget(makeHome());
    m_pages->addWidget(makeInstances());
    m_pages->addWidget(makeExplore());
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
    connect(&Sagiri::Config::get(), &Sagiri::Config::changed, this, [this]() { applyTheme(); });
    qApp->installEventFilter(this);

    applyTheme();
    refresh();
    updateAccount();
    loadPopular();
}

void BloqueWindow::closeEvent(QCloseEvent* e)
{
    qApp->removeEventFilter(this);
    e->accept();
    if (m_classic)
        m_classic->close();  // al cerrar la ventana clasica, Prism termina con normalidad
}

bool BloqueWindow::eventFilter(QObject* obj, QEvent* e)
{
    if (e->type() == QEvent::MouseButtonRelease) {
        if (auto* b = qobject_cast<QAbstractButton*>(obj)) {
            if (b->isEnabled() && b->window() == this)
                Sagiri::playSound("click");
        }
    }
    return QMainWindow::eventFilter(obj, e);
}

void BloqueWindow::applyTheme()
{
    auto& cfg = Sagiri::Config::get();
    const Sagiri::Theme t = cfg.theme();
    const bool hasBg = QFileInfo::exists(cfg.value("bg_path").toString());
    const bool translucent = t.glass || hasBg;
    const int alpha = cfg.value("glass_alpha", t.glass ? 18 : (hasBg ? 78 : 100)).toInt();
    const QString family = cfg.value("font").toString().isEmpty() ? t.font : cfg.value("font").toString();
    setStyleSheet(Sagiri::buildStyleSheet(t, translucent, alpha, family, cfg.value("font_size", 10).toInt()));
    if (centralWidget())
        centralWidget()->update();
}

void BloqueWindow::go(int page)
{
    if (page < 0 || page >= m_pages->count())
        return;
    if (auto* b = m_nav->button(page))
        b->setChecked(true);
    if (page == m_pages->currentIndex())
        return;
    m_pages->setCurrentIndex(page);
    if (auto* w = m_pages->currentWidget()) {
        auto* eff = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(eff);
        auto* a = new QPropertyAnimation(eff, "opacity", w);
        a->setDuration(240);
        a->setStartValue(0.0);
        a->setEndValue(1.0);
        connect(a, &QPropertyAnimation::finished, w, [w]() { w->setGraphicsEffect(nullptr); });
        a->start(QAbstractAnimation::DeleteWhenStopped);
    }
    if (page == 3)
        loadFeed(m_vidTabs->checkedId() < 0 ? 0 : m_vidTabs->checkedId());
}

void BloqueWindow::toast(const QString& text)
{
    Sagiri::playSound(text.startsWith("No ") || text.startsWith("Error") ? "err" : "note");
    auto* l = new QLabel(text, centralWidget());
    l->setStyleSheet("background:rgba(15,24,43,0.96);color:#ffffff;border-radius:14px;padding:10px 18px;font-weight:600;");
    l->adjustSize();
    l->move((width() - l->width()) / 2, height() - l->height() - 28);
    l->show();
    l->raise();
    QTimer::singleShot(3000, l, &QObject::deleteLater);
}

QString BloqueWindow::currentInstanceId() const
{
    if (m_grid && m_grid->currentItem())
        return m_grid->currentItem()->data(Qt::UserRole).toString();
    return m_carousel ? m_carousel->currentId() : QString();
}

QPixmap BloqueWindow::avatar(int px) const
{
    auto& cfg = Sagiri::Config::get();
    const QString path = cfg.value("avatar_path").toString();
    if (cfg.value("avatar_mode", "skin").toString() == "file" && QFileInfo::exists(path)) {
        const QPixmap pm(path);
        if (!pm.isNull())
            return Sagiri::roundedPixmap(pm, px, px, px / 4);
    }
    if (auto acc = APPLICATION->accounts()->defaultAccount()) {
        const QPixmap face = acc->getFace(px, px);
        if (!face.isNull())
            return Sagiri::roundedPixmap(face, px, px, px / 4);
    }
    return Sagiri::icon("user", Qt::white, px).pixmap(px, px);
}

void BloqueWindow::updateAccount()
{
    auto acc = APPLICATION->accounts()->defaultAccount();
    const QString name = acc ? acc->profileName() : tr("Sin cuenta");
    m_chip->setText(name);
    m_chip->setIcon(QIcon(avatar(28)));
    if (m_accAvatar) {
        m_accAvatar->setPixmap(avatar(72));
        m_accName->setText(QString("<h2>%1</h2>").arg(name.toHtmlEscaped()));
    }
    if (m_accounts) {
        m_accounts->clear();
        auto* list = APPLICATION->accounts();
        for (int i = 0; i < list->count(); ++i) {
            auto a = list->at(i);
            const bool def = (a.get() == acc.get());
            auto* it = new QListWidgetItem(QIcon(a->getFace(40, 40)), a->profileName() + (def ? tr("  (predeterminada)") : QString()), m_accounts);
            it->setData(Qt::UserRole, i);
        }
    }
}

void BloqueWindow::launch(const QString& id)
{
    auto* inst = APPLICATION->instances()->getInstanceById(id);
    if (inst && !inst->isRunning()) {
        Sagiri::playSound("launch");
        APPLICATION->launch(inst);
    }
}

void BloqueWindow::editInstance(const QString& id)
{
    if (auto* inst = APPLICATION->instances()->getInstanceById(id))
        APPLICATION->showInstanceWindow(inst);
}

void BloqueWindow::openFolder(const QString& id)
{
    if (auto* inst = APPLICATION->instances()->getInstanceById(id))
        QDesktopServices::openUrl(QUrl::fromLocalFile(inst->instanceRoot()));
}

void BloqueWindow::changeIcon(const QString& id)
{
    auto* inst = APPLICATION->instances()->getInstanceById(id);
    if (!inst)
        return;
    IconPickerDialog dlg(this);
    dlg.execWithSelection(inst->iconKey());
    if (dlg.result() == QDialog::Accepted) {
        inst->setIconKey(dlg.selectedIconKey);
        refresh();
    }
}

void BloqueWindow::deleteInstance(const QString& id)
{
    auto* inst = APPLICATION->instances()->getInstanceById(id);
    if (!inst)
        return;
    if (inst->isRunning()) {
        toast(tr("No se puede eliminar una instancia que esta abierta."));
        return;
    }
    const auto r = CustomMessageBox::selectable(this, tr("Eliminar instancia"),
                                                tr("Vas a eliminar \"%1\" y todos sus archivos (mundos, mods, ajustes).\n\nSeguro que quieres continuar?").arg(inst->name()),
                                                QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                       ->exec();
    if (r != QMessageBox::Yes)
        return;
    if (!APPLICATION->instances()->trashInstance(id))
        APPLICATION->instances()->deleteInstance(id);
    toast(tr("Instancia eliminada."));
}

void BloqueWindow::openSkins()
{
    auto account = APPLICATION->accounts()->defaultAccount();
    if (account && (account->accountType() == AccountType::MSA) && !account->isActive()) {
        SkinManageDialog dialog(this, account);
        dialog.exec();
    } else {
        toast(tr("Inicia sesion con una cuenta de Microsoft para gestionar tus skins."));
    }
}

void BloqueWindow::refresh()
{
    QList<BloqueCarousel::Item> items;
    const QString keep = currentInstanceId();
    m_quick->clear();
    m_grid->clear();
    m_exInst->clear();
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        auto* inst = list->at(i);
        const QIcon icon = APPLICATION->icons()->getIcon(inst->iconKey());
        m_exInst->addItem(icon, inst->name(), inst->id());
        if (!m_filter.isEmpty() && !inst->name().contains(m_filter, Qt::CaseInsensitive))
            continue;
        BloqueCarousel::Item it;
        it.id = inst->id();
        it.name = inst->name();
        it.sub = describe(inst);
        it.icon = icon.pixmap(128, 128);
        it.hue = int(qHash(it.id) % 360);
        items << it;
        auto* q = new QListWidgetItem(icon, inst->name(), m_quick);
        q->setData(Qt::UserRole, it.id);
        auto* g = new QListWidgetItem(icon, inst->name(), m_grid);
        g->setData(Qt::UserRole, it.id);
        g->setToolTip(it.sub);
        if (it.id == keep)
            m_grid->setCurrentItem(g);
    }
    m_carousel->setItems(items);
    updateDetail();
}

void BloqueWindow::updateDetail()
{
    auto* inst = APPLICATION->instances()->getInstanceById(currentInstanceId());
    m_detPanel->setVisible(inst != nullptr);
    if (!inst)
        return;
    m_detIcon->setPixmap(APPLICATION->icons()->getIcon(inst->iconKey()).pixmap(96, 96));
    m_detName->setText(QString("<h2>%1</h2>").arg(inst->name().toHtmlEscaped()));
    m_detSub->setText(describe(inst));
}

// ------------------------------------------------------------------ Modrinth
void BloqueWindow::modrinthSearch(const QString& type, const QString& query, int offset, const std::function<void(const QJsonArray&)>& cb)
{
    QUrl url("https://api.modrinth.com/v2/search");
    QUrlQuery q;
    q.addQueryItem("query", query);
    QJsonArray facetInner;
    facetInner.append("project_type:" + type);
    QJsonArray facets;
    facets.append(facetInner);
    q.addQueryItem("facets", QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));
    q.addQueryItem("limit", "24");
    q.addQueryItem("offset", QString::number(offset));
    q.addQueryItem("index", query.isEmpty() ? "downloads" : "relevance");
    url.setQuery(q);
    Http::get().json(url, this, [cb](const QJsonDocument& d) { cb(d.object().value("hits").toArray()); });
}

void BloqueWindow::loadPopular()
{
    modrinthSearch("modpack", QString(), 0, [this](const QJsonArray& hits) {
        clearLayout(m_popularRow);
        if (hits.isEmpty()) {
            m_popularRow->addWidget(new QLabel(tr("No pude cargar los modpacks (revisa tu internet)."), this));
            return;
        }
        for (const auto& hv : hits) {
            const QJsonObject h = hv.toObject();
            const QString id = h.value("project_id").toString(), name = h.value("title").toString();
            auto* c = projectCard(this, h, tr("Instalar"), [this, id, name]() { installModpack(id, name); });
            c->setFixedWidth(250);
            m_popularRow->addWidget(c);
        }
        m_popularRow->addStretch(1);
    });
}

void BloqueWindow::installModpack(const QString& projectId, const QString& name)
{
    toast(tr("Preparando \"%1\"...").arg(name));
    Http::get().json(QUrl("https://api.modrinth.com/v2/project/" + projectId + "/version"), this, [this, name](const QJsonDocument& d) {
        const QJsonArray versions = d.array();
        if (versions.isEmpty()) {
            toast(tr("No pude obtener las versiones de \"%1\".").arg(name));
            return;
        }
        QJsonObject file;
        for (const auto& fv : versions.first().toObject().value("files").toArray()) {
            const QJsonObject f = fv.toObject();
            if (f.value("filename").toString().endsWith(".mrpack") || f.value("primary").toBool()) {
                file = f;
                break;
            }
        }
        if (file.isEmpty()) {
            toast(tr("No encontre el archivo del modpack."));
            return;
        }
        const QString fname = file.value("filename").toString();
        Http::get().bytes(QUrl(file.value("url").toString()), this, [this, fname, name](const QByteArray& b) {
            if (b.isEmpty()) {
                toast(tr("No pude descargar el modpack."));
                return;
            }
            const QString path = QDir::temp().filePath("sagiri-" + fname);
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                toast(tr("No pude guardar el modpack descargado."));
                return;
            }
            f.write(b);
            f.close();
            // se abre el asistente de Prism con el archivo ya cargado: instala el modpack completo con sus mods
            if (m_classic)
                m_classic->addInstance(QUrl::fromLocalFile(path).toString());
        });
    });
}

void BloqueWindow::fetchVersion(const QString& projectId, const QString& type, const QString& mc, const QString& loader, bool filtered,
                                const std::function<void(const QJsonObject&)>& cb)
{
    QUrl url("https://api.modrinth.com/v2/project/" + projectId + "/version");
    QUrlQuery q;
    if (filtered && !mc.isEmpty()) {
        QJsonArray gv;
        gv.append(mc);
        q.addQueryItem("game_versions", QString::fromUtf8(QJsonDocument(gv).toJson(QJsonDocument::Compact)));
    }
    if (filtered && type == "mod" && !loader.isEmpty()) {
        QJsonArray lv;
        lv.append(loader);
        q.addQueryItem("loaders", QString::fromUtf8(QJsonDocument(lv).toJson(QJsonDocument::Compact)));
    }
    url.setQuery(q);
    Http::get().json(url, this, [this, projectId, type, mc, loader, filtered, cb](const QJsonDocument& d) {
        const QJsonArray arr = d.array();
        if (arr.isEmpty() && filtered && type != "mod") {
            fetchVersion(projectId, type, mc, loader, false, cb);
            return;
        }
        cb(arr.isEmpty() ? QJsonObject() : arr.first().toObject());
    });
}

void BloqueWindow::downloadVersionFile(const QJsonObject& version, const QString& dir, const std::function<void(bool, const QString&)>& done)
{
    QJsonObject file;
    for (const auto& fv : version.value("files").toArray()) {
        const QJsonObject f = fv.toObject();
        if (f.value("primary").toBool()) {
            file = f;
            break;
        }
        if (file.isEmpty())
            file = f;
    }
    if (file.isEmpty()) {
        done(false, tr("No hay archivos en esa version."));
        return;
    }
    const QString name = file.value("filename").toString();
    const QString sha1 = file.value("hashes").toObject().value("sha1").toString();
    Http::get().bytes(QUrl(file.value("url").toString()), this, [this, dir, name, sha1, done](const QByteArray& b) {
        if (b.isEmpty()) {
            done(false, tr("No pude descargar el archivo."));
            return;
        }
        if (!sha1.isEmpty() && QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex() != sha1.toLatin1()) {
            done(false, tr("No coincide la firma del archivo descargado."));
            return;
        }
        QDir().mkpath(dir);
        QFile f(QDir(dir).filePath(name));
        if (!f.open(QIODevice::WriteOnly)) {
            done(false, tr("No pude guardar %1").arg(name));
            return;
        }
        f.write(b);
        f.close();
        done(true, QString());
    });
}

void BloqueWindow::installProject(const QString& projectId, const QString& type, const QString& instId, const QString& name)
{
    auto* inst = APPLICATION->instances()->getInstanceById(instId);
    if (!inst) {
        toast(tr("No hay una instancia elegida: crea una primero."));
        return;
    }
    QString mc, loader;
    if (auto* pp = inst->getPackProfile()) {
        mc = pp->getComponentVersion("net.minecraft");
        loader = loaderName(pp).toLower();
    }
    const QString dir = type == "mod" ? inst->modsRoot() : (type == "resourcepack" ? inst->resourcePacksDir() : inst->shaderPacksDir());
    toast(tr("Descargando \"%1\"...").arg(name));
    fetchVersion(projectId, type, mc, loader, true, [this, dir, name, mc, loader, type](const QJsonObject& v) {
        if (v.isEmpty()) {
            toast(tr("No encontre una version compatible de \"%1\".").arg(name));
            return;
        }
        downloadVersionFile(v, dir, [this, name](bool ok, const QString& msg) { toast(ok ? tr("\"%1\" instalado.").arg(name) : msg); });
        if (type != "mod")
            return;
        for (const auto& dv : v.value("dependencies").toArray()) {
            const QJsonObject d = dv.toObject();
            const QString dep = d.value("project_id").toString();
            if (d.value("dependency_type").toString() != "required" || dep.isEmpty())
                continue;
            fetchVersion(dep, "mod", mc, loader, true, [this, dir](const QJsonObject& dv2) {
                if (!dv2.isEmpty())
                    downloadVersionFile(dv2, dir, [](bool, const QString&) {});
            });
        }
    });
}

void BloqueWindow::loadExplore(bool reset)
{
    if (reset) {
        m_exOffset = 0;
        m_exCount = 0;
        clearLayout(m_exGrid);
        m_exMore->hide();
    }
    const int token = ++m_exToken;
    const QString type = m_exType;
    modrinthSearch(type, m_exSearch->text().trimmed(), m_exOffset, [this, token, type](const QJsonArray& hits) {
        if (token != m_exToken)
            return;
        if (hits.isEmpty() && m_exCount == 0) {
            m_exGrid->addWidget(new QLabel(tr("No hay resultados (o no hay internet)."), m_exHost), 0, 0);
            return;
        }
        for (const auto& hv : hits) {
            const QJsonObject h = hv.toObject();
            const QString id = h.value("project_id").toString(), name = h.value("title").toString();
            const QString btn = type == "modpack" ? tr("Instalar") : tr("Instalar en la instancia");
            auto* c = projectCard(m_exHost, h, btn, [this, id, name, type]() {
                if (type == "modpack")
                    installModpack(id, name);
                else
                    installProject(id, type, m_exInst->currentData().toString(), name);
            });
            m_exGrid->addWidget(c, m_exCount / 3, m_exCount % 3);
            ++m_exCount;
        }
        m_exOffset += int(hits.size());
        m_exMore->setVisible(hits.size() >= 24);
    });
}

// -------------------------------------------------------------------- Videos
void BloqueWindow::loadFeed(int which)
{
    if (which < 0 || which > 2 || m_vidLoaded[which])
        return;
    m_vidLoaded[which] = true;
    QGridLayout* grid = m_vidGrid[which];
    clearLayout(grid);
    grid->addWidget(new QLabel(tr("Cargando..."), m_vidStack), 0, 0);
    auto fail = [this, grid, which]() {
        clearLayout(grid);
        grid->addWidget(new QLabel(tr("No pude cargar esta lista (revisa tu internet). Usa los botones de arriba para abrirla en YouTube."), m_vidStack), 0, 0);
        m_vidLoaded[which] = false;
    };
    if (which < 2) {
        Http::get().bytes(QUrl(kFeeds[which].rss), this, [this, grid, fail](const QByteArray& b) {
            const QList<Vid> vids = parseFeed(b);
            if (vids.isEmpty()) {
                fail();
                return;
            }
            clearLayout(grid);
            int n = 0;
            for (const auto& v : vids) {
                grid->addWidget(mediaCard(m_vidStack, v.title, v.published.left(10), QUrl("https://i.ytimg.com/vi/" + v.id + "/mqdefault.jpg"),
                                          QUrl("https://www.youtube.com/watch?v=" + v.id)),
                                n / 3, n % 3);
                ++n;
            }
        });
    } else {
        Http::get().json(QUrl("https://launchercontent.mojang.com/v2/javaPatchNotes.json"), this, [this, grid, fail](const QJsonDocument& d) {
            const QJsonArray entries = d.object().value("entries").toArray();
            if (entries.isEmpty()) {
                fail();
                return;
            }
            clearLayout(grid);
            int n = 0;
            for (const auto& ev : entries) {
                if (n >= 24)
                    break;
                const QJsonObject e = ev.toObject();
                const QString version = e.value("version").toString();
                QString img = e.value("image").toObject().value("url").toString();
                if (img.startsWith("/"))
                    img = "https://launchercontent.mojang.com" + img;
                const QString sub = e.value("type").toString() + " - " + e.value("date").toString().left(10);
                grid->addWidget(mediaCard(m_vidStack, e.value("title").toString(), sub, QUrl(img), QUrl("https://minecraft.wiki/w/Java_Edition_" + version)), n / 3, n % 3);
                ++n;
            }
        });
    }
}

// --------------------------------------------------------------------- Paginas
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
    m_popularRow = new QHBoxLayout(row);
    m_popularRow->setContentsMargins(0, 0, 0, 0);
    m_popularRow->setSpacing(12);
    m_popularRow->addWidget(new QLabel(tr("Cargando modpacks..."), row));
    auto* s = wrapScroll(row);
    s->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    s->setMinimumHeight(215);
    v->addWidget(s, 2);
    return w;
}

QWidget* BloqueWindow::makeInstances()
{
    auto* w = new QWidget();
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* left = new QVBoxLayout();
    left->addWidget(title(tr("Instancias"), tr("Elige una y usa el panel de la derecha. Doble clic para jugar.")));
    m_grid = new QListWidget(w);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setIconSize(QSize(72, 72));
    m_grid->setGridSize(QSize(150, 128));
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setWordWrap(true);
    left->addWidget(m_grid, 1);
    h->addLayout(left, 1);

    m_detPanel = new QFrame(w);
    m_detPanel->setObjectName("panel");
    m_detPanel->setFixedWidth(280);
    auto* dv = new QVBoxLayout(m_detPanel);
    dv->setSpacing(8);
    m_detIcon = new QLabel(m_detPanel);
    m_detIcon->setAlignment(Qt::AlignCenter);
    m_detName = new QLabel(m_detPanel);
    m_detName->setWordWrap(true);
    m_detName->setAlignment(Qt::AlignCenter);
    m_detSub = new QLabel(m_detPanel);
    m_detSub->setAlignment(Qt::AlignCenter);
    m_detSub->setStyleSheet("color:#9aa7c7;");
    dv->addWidget(m_detIcon);
    dv->addWidget(m_detName);
    dv->addWidget(m_detSub);
    dv->addWidget(action(m_detPanel, tr("Jugar"), true, [this]() { launch(currentInstanceId()); }, "play"));
    dv->addWidget(action(m_detPanel, tr("Editar (mods, versiones, ajustes)"), false, [this]() { editInstance(currentInstanceId()); }, "edit"));
    dv->addWidget(action(m_detPanel, tr("Cambiar icono"), false, [this]() { changeIcon(currentInstanceId()); }, "image"));
    dv->addWidget(action(m_detPanel, tr("Abrir carpeta"), false, [this]() { openFolder(currentInstanceId()); }, "folder"));
    auto* del = action(m_detPanel, tr("Eliminar instancia"), false, [this]() { deleteInstance(currentInstanceId()); }, "trash");
    del->setProperty("danger", true);
    dv->addWidget(del);
    dv->addStretch(1);
    dv->addWidget(action(m_detPanel, tr("Nueva instancia / importar"), false, [this]() { go(2); }, "plus"));
    h->addWidget(m_detPanel);
    m_detPanel->hide();

    connect(m_grid, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { launch(it->data(Qt::UserRole).toString()); });
    connect(m_grid, &QListWidget::currentItemChanged, this, [this]() { updateDetail(); });
    return w;
}

QWidget* BloqueWindow::makeExplore()
{
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(12);
    v->addWidget(title(tr("Explorar"), tr("Modpacks, mods, texturas y shaders de Modrinth. Elige una categoria y pulsa Instalar.")));
    auto* bar = new QHBoxLayout();
    auto* grp = new QButtonGroup(w);
    grp->setExclusive(true);
    const struct {
        const char* name;
        const char* type;
    } cats[] = { { "Modpacks", "modpack" }, { "Mods", "mod" }, { "Texturas", "resourcepack" }, { "Shaders", "shader" } };
    int i = 0;
    for (const auto& c : cats) {
        auto* b = new QPushButton(tr(c.name), w);
        b->setCheckable(true);
        b->setProperty("seg", true);
        grp->addButton(b, i++);
        bar->addWidget(b);
        const QString type = QString::fromLatin1(c.type);
        connect(b, &QPushButton::clicked, this, [this, type]() {
            m_exType = type;
            loadExplore(true);
        });
    }
    grp->button(0)->setChecked(true);
    bar->addStretch(1);
    bar->addWidget(new QLabel(tr("Instalar en:"), w));
    m_exInst = new QComboBox(w);
    m_exInst->setMinimumWidth(220);
    bar->addWidget(m_exInst);
    v->addLayout(bar);

    m_exSearch = new QLineEdit(w);
    m_exSearch->setPlaceholderText(tr("Buscar en Modrinth..."));
    m_exSearch->setMaximumWidth(520);
    m_exSearch->addAction(Sagiri::icon("search", Qt::white, 18), QLineEdit::LeadingPosition);
    v->addWidget(m_exSearch);
    m_exTimer.setSingleShot(true);
    m_exTimer.setInterval(420);
    connect(m_exSearch, &QLineEdit::textChanged, this, [this]() { m_exTimer.start(); });
    connect(&m_exTimer, &QTimer::timeout, this, [this]() { loadExplore(true); });

    m_exHost = new QWidget();
    auto* hv = new QVBoxLayout(m_exHost);
    hv->setContentsMargins(0, 0, 0, 0);
    auto* gw = new QWidget(m_exHost);
    m_exGrid = new QGridLayout(gw);
    m_exGrid->setContentsMargins(0, 0, 0, 0);
    m_exGrid->setSpacing(12);
    for (int c = 0; c < 3; ++c)
        m_exGrid->setColumnStretch(c, 1);
    hv->addWidget(gw);
    m_exMore = action(m_exHost, tr("Cargar mas"), false, [this]() { loadExplore(false); });
    m_exMore->hide();
    hv->addWidget(m_exMore);
    hv->addStretch(1);
    v->addWidget(wrapScroll(m_exHost), 1);
    QTimer::singleShot(0, this, [this]() { loadExplore(true); });
    return w;
}

QWidget* BloqueWindow::makeVideos()
{
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(12);
    v->addWidget(title(tr("Videos y noticias"), tr("Toca un video o una noticia para abrirlo directamente.")));
    auto* bar = new QHBoxLayout();
    m_vidTabs = new QButtonGroup(w);
    m_vidTabs->setExclusive(true);
    m_vidStack = new QStackedWidget(w);
    const char* names[3] = { "Vegetta777", "Pacheco Cara Floja", "Noticias de Minecraft" };
    for (int i = 0; i < 3; ++i) {
        auto* b = new QPushButton(tr(names[i]), w);
        b->setCheckable(true);
        b->setProperty("seg", true);
        m_vidTabs->addButton(b, i);
        bar->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, i]() {
            m_vidStack->setCurrentIndex(i);
            loadFeed(i);
        });
        auto* host = new QWidget();
        auto* hv = new QVBoxLayout(host);
        hv->setContentsMargins(0, 0, 0, 0);
        auto* gw = new QWidget(host);
        m_vidGrid[i] = new QGridLayout(gw);
        m_vidGrid[i]->setContentsMargins(0, 0, 0, 0);
        m_vidGrid[i]->setSpacing(12);
        hv->addWidget(gw);
        hv->addStretch(1);
        m_vidStack->addWidget(wrapScroll(host));
    }
    m_vidTabs->button(0)->setChecked(true);
    bar->addStretch(1);
    bar->addWidget(action(w, tr("Abrir canal"), false, [this]() {
        const int i = m_vidTabs->checkedId();
        if (i >= 0 && i < 2)
            QDesktopServices::openUrl(QUrl(kFeeds[i].channel));
        else
            QDesktopServices::openUrl(QUrl("https://www.minecraft.net/en-us/articles"));
    }, "video"));
    bar->addWidget(action(w, tr("Buscar en YouTube"), false, [this]() {
        const int i = m_vidTabs->checkedId();
        const QString q = i == 1 ? QString::fromLatin1(kFeeds[1].search) : (i == 0 ? QString::fromLatin1(kFeeds[0].search) : QString("minecraft novedades actualizacion"));
        QDesktopServices::openUrl(QUrl("https://www.youtube.com/results?search_query=" + QString::fromLatin1(QUrl::toPercentEncoding(q))));
    }, "search"));
    v->addLayout(bar);
    v->addWidget(m_vidStack, 1);
    return w;
}

QWidget* BloqueWindow::makeAccount()
{
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(14);
    v->addWidget(title(tr("Cuenta"), tr("Tu cuenta de Minecraft, tu foto de perfil y tus skins.")));
    auto* head = new QFrame(w);
    head->setObjectName("panel");
    auto* hl = new QHBoxLayout(head);
    m_accAvatar = new QLabel(head);
    m_accAvatar->setFixedSize(72, 72);
    hl->addWidget(m_accAvatar);
    auto* col = new QVBoxLayout();
    m_accName = new QLabel(head);
    col->addWidget(m_accName);
    auto* btns = new QHBoxLayout();
    btns->addWidget(action(head, tr("Editar skins y capas"), true, [this]() { openSkins(); }, "shirt"));
    btns->addWidget(action(head, tr("Foto de perfil: elegir imagen"), false, [this]() {
        const QString f = QFileDialog::getOpenFileName(this, tr("Elegir foto de perfil"), QString(), tr("Imagenes (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (f.isEmpty())
            return;
        auto& cfg = Sagiri::Config::get();
        cfg.setValue("avatar_path", f);
        cfg.setValue("avatar_mode", "file");
        updateAccount();
    }, "image"));
    btns->addWidget(action(head, tr("Usar la cabeza de mi skin"), false, [this]() {
        Sagiri::Config::get().setValue("avatar_mode", "skin");
        updateAccount();
    }, "user"));
    btns->addStretch(1);
    col->addLayout(btns);
    hl->addLayout(col, 1);
    v->addWidget(head);

    auto* accPanel = new QFrame(w);
    accPanel->setObjectName("panel");
    auto* av = new QVBoxLayout(accPanel);
    av->addWidget(new QLabel(tr("<h3>Cuentas de Minecraft</h3>")));
    m_accounts = new QListWidget(accPanel);
    m_accounts->setIconSize(QSize(40, 40));
    av->addWidget(m_accounts, 1);
    auto* ab = new QHBoxLayout();
    ab->addWidget(action(accPanel, tr("Anadir cuenta de Microsoft"), true, [this]() {
        auto account = MSALoginDialog::newAccount(this);
        if (account) {
            APPLICATION->accounts()->addAccount(account);
            if (APPLICATION->accounts()->count() == 1)
                APPLICATION->accounts()->setDefaultAccount(account);
        }
    }, "plus"));
    ab->addWidget(action(accPanel, tr("Usar como predeterminada"), false, [this]() {
        if (!m_accounts->currentItem())
            return;
        APPLICATION->accounts()->setDefaultAccount(APPLICATION->accounts()->at(m_accounts->currentItem()->data(Qt::UserRole).toInt()));
    }, "check"));
    ab->addWidget(action(accPanel, tr("Quitar"), false, [this]() {
        if (!m_accounts->currentItem())
            return;
        APPLICATION->accounts()->removeAccount(APPLICATION->accounts()->index(m_accounts->currentItem()->data(Qt::UserRole).toInt()));
    }, "trash"));
    ab->addStretch(1);
    av->addLayout(ab);
    v->addWidget(accPanel, 1);
    return w;
}

QWidget* BloqueWindow::makeSettings()
{
    auto* w = new QWidget();
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(title(tr("Ajustes"), tr("Todo se cambia aqui mismo, sin ventanas aparte.")));
    auto* tabs = new QTabWidget(w);
    auto& cfg = Sagiri::Config::get();

    // ---- Apariencia
    auto* ap = new QWidget();
    auto* av = new QGridLayout(ap);
    av->setAlignment(Qt::AlignTop);
    int r = 0;
    auto label = [ap](const QString& t) { return new QLabel(t, ap); };
    auto* themeBox = new QComboBox(ap);
    const struct {
        const char* id;
        const char* name;
    } themes[] = { { "modern", "Moderno" }, { "glass", "iOS 26 Cristal" }, { "steam", "Steam" }, { "minecraft", "Minecraft" }, { "custom", "Personalizado" } };
    for (const auto& t : themes)
        themeBox->addItem(tr(t.name), QString::fromLatin1(t.id));
    themeBox->setCurrentIndex(qMax(0, themeBox->findData(cfg.themeId())));
    connect(themeBox, &QComboBox::activated, this, [themeBox]() { Sagiri::Config::get().setThemeId(themeBox->currentData().toString()); });
    av->addWidget(label(tr("Tema")), r, 0);
    av->addWidget(themeBox, r++, 1);

    auto* fontBox = new QComboBox(ap);
    fontBox->addItem(tr("Predeterminada"), QString());
    for (const QString& fam : QFontDatabase::families())
        fontBox->addItem(fam, fam);
    fontBox->setCurrentIndex(qMax(0, fontBox->findData(cfg.value("font").toString())));
    connect(fontBox, &QComboBox::activated, this, [fontBox]() { Sagiri::Config::get().setValue("font", fontBox->currentData().toString()); });
    av->addWidget(label(tr("Fuente de letra")), r, 0);
    av->addWidget(fontBox, r++, 1);

    auto* sizeSpin = new QSpinBox(ap);
    sizeSpin->setRange(8, 16);
    sizeSpin->setValue(cfg.value("font_size", 10).toInt());
    connect(sizeSpin, &QSpinBox::valueChanged, this, [](int val) { Sagiri::Config::get().setValue("font_size", val); });
    av->addWidget(label(tr("Tamano de letra")), r, 0);
    av->addWidget(sizeSpin, r++, 1);

    const struct {
        const char* key;
        const char* name;
    } colors[] = { { "c_bg", "Color de fondo" }, { "c_panel", "Color de paneles" }, { "c_text", "Color del texto" }, { "c_accent", "Color de acento" } };
    for (const auto& c : colors) {
        auto* b = new QPushButton(tr("Elegir..."), ap);
        const QString key = QString::fromLatin1(c.key);
        connect(b, &QPushButton::clicked, this, [this, key]() {
            const QColor col = QColorDialog::getColor(QColor(Sagiri::Config::get().value(key).toString()), this, tr("Elegir color"));
            if (col.isValid()) {
                Sagiri::Config::get().setValue(key, col.name());
                Sagiri::Config::get().setThemeId("custom");
            }
        });
        av->addWidget(label(tr(c.name) + tr(" (tema Personalizado)")), r, 0);
        av->addWidget(b, r++, 1);
    }

    auto* bgRow = new QHBoxLayout();
    bgRow->addWidget(action(ap, tr("Elegir imagen de fondo..."), true, [this]() {
        const QString f = QFileDialog::getOpenFileName(this, tr("Imagen de fondo"), QString(), tr("Imagenes (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (!f.isEmpty())
            Sagiri::Config::get().setValue("bg_path", f);
    }, "image"));
    bgRow->addWidget(action(ap, tr("Quitar fondo"), false, []() { Sagiri::Config::get().setValue("bg_path", QString()); }, "trash"));
    bgRow->addStretch(1);
    av->addWidget(label(tr("Fondo del launcher")), r, 0);
    av->addLayout(bgRow, r++, 1);

    auto* alpha = new QSlider(Qt::Horizontal, ap);
    alpha->setRange(0, 100);
    alpha->setValue(cfg.value("glass_alpha", cfg.theme().glass ? 18 : 78).toInt());
    connect(alpha, &QSlider::valueChanged, this, [](int val) { Sagiri::Config::get().setValue("glass_alpha", val); });
    av->addWidget(label(tr("Transparencia de paneles (con fondo o cristal)")), r, 0);
    av->addWidget(alpha, r++, 1);
    av->setColumnStretch(1, 1);
    tabs->addTab(ap, Sagiri::icon("palette", Qt::white, 18), tr("Apariencia"));

    // ---- Sonido
    auto* so = new QWidget();
    auto* sl = new QGridLayout(so);
    sl->setAlignment(Qt::AlignTop);
    auto* snd = new QCheckBox(tr("Sonidos suaves en la interfaz (Windows)"), so);
    snd->setChecked(cfg.value("sound", true).toBool());
    connect(snd, &QCheckBox::toggled, this, [](bool on) { Sagiri::Config::get().setValue("sound", on); });
    sl->addWidget(snd, 0, 0, 1, 2);
    auto* vol = new QSlider(Qt::Horizontal, so);
    vol->setRange(0, 100);
    vol->setValue(cfg.value("volume", 40).toInt());
    connect(vol, &QSlider::valueChanged, this, [](int val) { Sagiri::Config::get().setValue("volume", val); });
    sl->addWidget(new QLabel(tr("Volumen"), so), 1, 0);
    sl->addWidget(vol, 1, 1);
    sl->addWidget(action(so, tr("Probar sonido"), false, []() { Sagiri::playSound("done"); }, "volume"), 2, 0);
    sl->setColumnStretch(1, 1);
    tabs->addTab(so, Sagiri::icon("volume", Qt::white, 18), tr("Sonido"));

    // ---- Java y memoria
    auto* ja = new QWidget();
    auto* jl = new QGridLayout(ja);
    jl->setAlignment(Qt::AlignTop);
    auto settings = APPLICATION->settings();
    auto* minMem = new QSpinBox(ja);
    minMem->setRange(256, 65536);
    minMem->setSingleStep(256);
    minMem->setSuffix(" MB");
    minMem->setValue(settings->get("MinMemAlloc").toInt());
    connect(minMem, &QSpinBox::valueChanged, this, [](int val) { APPLICATION->settings()->set("MinMemAlloc", val); });
    auto* maxMem = new QSpinBox(ja);
    maxMem->setRange(512, 65536);
    maxMem->setSingleStep(256);
    maxMem->setSuffix(" MB");
    maxMem->setValue(settings->get("MaxMemAlloc").toInt());
    connect(maxMem, &QSpinBox::valueChanged, this, [](int val) { APPLICATION->settings()->set("MaxMemAlloc", val); });
    auto* javaPath = new QLineEdit(settings->get("JavaPath").toString(), ja);
    javaPath->setPlaceholderText(tr("vacio = automatico"));
    connect(javaPath, &QLineEdit::editingFinished, this, [javaPath]() { APPLICATION->settings()->set("JavaPath", javaPath->text()); });
    auto* jvm = new QLineEdit(settings->get("JvmArgs").toString(), ja);
    connect(jvm, &QLineEdit::editingFinished, this, [jvm]() { APPLICATION->settings()->set("JvmArgs", jvm->text()); });
    auto* console = new QCheckBox(tr("Mostrar la consola del juego"), ja);
    console->setChecked(settings->get("ShowConsole").toBool());
    connect(console, &QCheckBox::toggled, this, [](bool on) { APPLICATION->settings()->set("ShowConsole", on); });
    auto* closeAfter = new QCheckBox(tr("Cerrar SagiriPrism al abrir el juego"), ja);
    closeAfter->setChecked(settings->get("CloseAfterLaunch").toBool());
    connect(closeAfter, &QCheckBox::toggled, this, [](bool on) { APPLICATION->settings()->set("CloseAfterLaunch", on); });
    jl->addWidget(new QLabel(tr("Memoria minima"), ja), 0, 0);
    jl->addWidget(minMem, 0, 1);
    jl->addWidget(new QLabel(tr("Memoria maxima"), ja), 1, 0);
    jl->addWidget(maxMem, 1, 1);
    jl->addWidget(new QLabel(tr("Ruta de Java"), ja), 2, 0);
    jl->addWidget(javaPath, 2, 1);
    jl->addWidget(new QLabel(tr("Argumentos de Java"), ja), 3, 0);
    jl->addWidget(jvm, 3, 1);
    jl->addWidget(console, 4, 0, 1, 2);
    jl->addWidget(closeAfter, 5, 0, 1, 2);
    jl->setColumnStretch(1, 1);
    tabs->addTab(ja, Sagiri::icon("gear", Qt::white, 18), tr("Java y juego"));

    // ---- Acerca de
    auto* ab = new QWidget();
    auto* abl = new QVBoxLayout(ab);
    abl->setAlignment(Qt::AlignTop);
    abl->addWidget(new QLabel(tr("<h3>SagiriPrism</h3><p>Hecho sobre Prism Launcher (GPL-3.0). Uso personal.</p>")));
    abl->addWidget(action(ab, tr("Ajustes avanzados de Prism (ventana clasica)"), false, [this]() { QMetaObject::invokeMethod(m_classic, "on_actionSettings_triggered"); }));
    tabs->addTab(ab, tr("Acerca de"));

    v->addWidget(tabs, 1);
    return w;
}
