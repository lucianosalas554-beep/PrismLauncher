// SPDX-License-Identifier: GPL-3.0-only
#include "SagiriPages.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "SagiriCore.h"
#include "icons/IconList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/skins/SkinRequests.h"
#include "net/NetJob.h"
#include "settings/SettingsObject.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/skins/SkinManageDialog.h"
#include "ui/dialogs/skins/edit/SkinEditorDialog.h"

namespace {

QScrollArea* wrapScroll(QWidget* inner)
{
    auto* s = new QScrollArea();
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    s->setWidget(inner);
    return s;
}

QString loaderOf(MinecraftInstance* inst, QString* mc = nullptr)
{
    QString loader;
    if (auto* pp = inst->getPackProfile()) {
        if (mc)
            *mc = pp->getComponentVersion("net.minecraft");
        if (!pp->getComponentVersion("net.neoforged").isEmpty())
            loader = "NeoForge";
        else if (!pp->getComponentVersion("net.minecraftforge").isEmpty())
            loader = "Forge";
        else if (!pp->getComponentVersion("net.fabricmc.fabric-loader").isEmpty())
            loader = "Fabric";
        else if (!pp->getComponentVersion("org.quiltmc.quilt-loader").isEmpty())
            loader = "Quilt";
    }
    return loader;
}

QString kindLabel(const QString& k)
{
    return k == "mod" ? QObject::tr("Mod") : (k == "res" ? QObject::tr("Resource pack") : QObject::tr("Shader"));
}

// Vista frontal del personaje a partir de la textura (cabeza, cuerpo, brazos, piernas y capa exterior).
QPixmap frontView(const QImage& tex, bool slim, int scale)
{
    QImage t = tex.convertToFormat(QImage::Format_ARGB32);
    if (t.size() != QSize(64, 64)) {
        QImage big(64, 64, QImage::Format_ARGB32);
        big.fill(Qt::transparent);
        QPainter bp(&big);
        bp.drawImage(0, 0, t);
        bp.end();
        t = big;
    }
    QImage out(16, 32, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    auto blit = [&](int sx, int sy, int w, int h, int dx, int dy) { p.drawImage(QPoint(dx, dy), t, QRect(sx, sy, w, h)); };
    const int a = slim ? 3 : 4;
    const int rarm[2][2] = { { 44, 20 }, { 44, 36 } }, larm[2][2] = { { 36, 52 }, { 52, 52 } };
    const int rleg[2][2] = { { 4, 20 }, { 4, 36 } }, lleg[2][2] = { { 20, 52 }, { 4, 52 } };
    for (int ov = 0; ov < 2; ++ov) {
        blit(ov ? 40 : 8, 8, 8, 8, 4, 0);
        blit(20, ov ? 36 : 20, 8, 12, 4, 8);
        blit(rarm[ov][0], rarm[ov][1], a, 12, 4 - a, 8);
        blit(larm[ov][0], larm[ov][1], a, 12, 12, 8);
        blit(rleg[ov][0], rleg[ov][1], 4, 12, 4, 20);
        blit(lleg[ov][0], lleg[ov][1], 4, 12, 8, 20);
    }
    p.end();
    return QPixmap::fromImage(out.scaled(16 * scale, 32 * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation));
}

QString slugOf(const QString& name)
{
    QString s = name.normalized(QString::NormalizationForm_D);
    s.remove(QRegularExpression("[^A-Za-z0-9._ -]"));
    s = s.trimmed();
    s.replace(' ', '-');
    return s.isEmpty() ? QString("instancia") : s.left(40);
}

bool compatibleWith(const QJsonObject& v, const QString& mc)
{
    for (const auto& r : v.value("requires").toArray()) {
        const QJsonObject o = r.toObject();
        if (o.value("uid").toString() == "net.minecraft" && o.contains("equals") && o.value("equals").toString() != mc)
            return false;
    }
    return true;
}

}  // namespace

// ================================================================ InstancePage
InstancePage::InstancePage(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(12);
    v->addWidget(Sagiri::makeButton(this, tr("Instancias"), false, [this]() { emit backRequested(); }, "chevl"), 0, Qt::AlignLeft);

    // cabecera
    auto* head = new QHBoxLayout();
    head->setSpacing(16);
    m_icon = new QLabel(this);
    m_icon->setFixedSize(84, 84);
    head->addWidget(m_icon);
    auto* col = new QVBoxLayout();
    m_name = new QLabel(this);
    m_meta = new QLabel(this);
    m_meta->setStyleSheet("color:#9aa7c7;");
    col->addWidget(m_name);
    col->addWidget(m_meta);
    head->addLayout(col, 1);
    auto* play = Sagiri::makeButton(this, tr("Jugar"), true, [this]() { emit playRequested(m_id); }, "play");
    play->setMinimumHeight(44);
    play->setMinimumWidth(130);
    head->addWidget(play);
    auto* gear = Sagiri::makeButton(this, QString(), false, [this]() { emit advancedRequested(m_id); }, "gear");
    gear->setToolTip(tr("Ajustes avanzados de la instancia (versiones, Java, componentes)"));
    gear->setMinimumHeight(44);
    head->addWidget(gear);
    v->addLayout(head);

    // pestanas
    auto* tabs = new QHBoxLayout();
    auto* grp = new QButtonGroup(this);
    grp->setExclusive(true);
    m_stack = new QStackedWidget(this);
    const struct {
        const char* name;
        const char* icon;
    } names[5] = { { "Contenido", "box" }, { "Archivos", "folder" }, { "Mundos", "store" }, { "Registros", "news" }, { "Compartir", "user" } };
    for (int i = 0; i < 5; ++i) {
        auto* b = new QPushButton(tr(names[i].name), this);
        b->setCheckable(true);
        b->setProperty("seg", true);
        b->setIcon(Sagiri::icon(names[i].icon, Qt::white, 18));
        grp->addButton(b, i);
        tabs->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, i]() {
            m_stack->setCurrentIndex(i);
            if (i == 3)
                loadLog();
        });
    }
    grp->button(0)->setChecked(true);
    tabs->addStretch(1);
    v->addLayout(tabs);

    // ---- Contenido
    auto* cw = new QWidget();
    auto* cv = new QVBoxLayout(cw);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(10);
    auto* bar = new QHBoxLayout();
    m_search = new QLineEdit(cw);
    m_search->setPlaceholderText(tr("Buscar proyectos..."));
    m_search->addAction(Sagiri::icon("search", Qt::white, 18), QLineEdit::LeadingPosition);
    bar->addWidget(m_search, 1);
    bar->addWidget(Sagiri::makeButton(cw, tr("Cargar archivos"), false, [this]() {
        auto* inst = APPLICATION->instances()->getInstanceById(m_id);
        if (!inst)
            return;
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Elegir archivos"), QString(), tr("Mods y paquetes (*.jar *.zip)"));
        for (const QString& f : files) {
            const QString dir = f.endsWith(".jar", Qt::CaseInsensitive) ? inst->modsRoot() : inst->resourcePacksDir();
            QDir().mkpath(dir);
            QFile::copy(f, QDir(dir).filePath(QFileInfo(f).fileName()));
        }
        if (!files.isEmpty())
            reloadContent();
    }, "folder"));
    bar->addWidget(Sagiri::makeButton(cw, tr("Explorar contenido"), true, [this]() { emit exploreRequested(m_id); }, "store"));
    cv->addLayout(bar);

    auto* chipsRow = new QHBoxLayout();
    auto* chips = new QButtonGroup(cw);
    chips->setExclusive(true);
    const char* chipNames[4] = { "Todo", "Mods", "Resource Packs", "Shaders" };
    for (int i = 0; i < 4; ++i) {
        auto* b = new QPushButton(tr(chipNames[i]), cw);
        b->setCheckable(true);
        b->setProperty("seg", true);
        chips->addButton(b, i);
        chipsRow->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, i]() {
            m_filter = i;
            applyFilter();
        });
    }
    chips->button(0)->setChecked(true);
    chipsRow->addStretch(1);
    m_count = new QLabel(cw);
    m_count->setStyleSheet("color:#9aa7c7;");
    chipsRow->addWidget(m_count);
    cv->addLayout(chipsRow);

    m_rowsHost = new QWidget();
    m_rows = new QVBoxLayout(m_rowsHost);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(8);
    m_rows->addStretch(1);
    cv->addWidget(wrapScroll(m_rowsHost), 1);
    m_stack->addWidget(cw);
    connect(m_search, &QLineEdit::textChanged, this, [this]() { applyFilter(); });

    // ---- Archivos
    auto* fw = new QWidget();
    auto* fv = new QVBoxLayout(fw);
    fv->setAlignment(Qt::AlignTop);
    fv->addWidget(new QLabel(tr("<h3>Carpetas de la instancia</h3>")));
    const struct {
        const char* name;
        const char* sub;
    } folders[] = { { "Carpeta de la instancia", "" }, { "mods", "mods" }, { "resourcepacks", "resourcepacks" }, { "shaderpacks", "shaderpacks" },
                    { "saves (mundos)", "saves" }, { "config", "config" }, { "logs", "logs" } };
    for (const auto& f : folders) {
        const QString sub = QString::fromLatin1(f.sub);
        fv->addWidget(Sagiri::makeButton(fw, tr(f.name), false, [this, sub]() {
            if (auto* inst = APPLICATION->instances()->getInstanceById(m_id)) {
                const QString dir = sub.isEmpty() ? inst->instanceRoot() : QDir(inst->gameRoot()).filePath(sub);
                QDir().mkpath(dir);
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
            }
        }, "folder"));
    }
    m_stack->addWidget(fw);

    // ---- Mundos
    m_worlds = new QListWidget(this);
    m_stack->addWidget(m_worlds);

    // ---- Registros
    auto* lw = new QWidget();
    auto* lv = new QVBoxLayout(lw);
    lv->setContentsMargins(0, 0, 0, 0);
    auto* lbar = new QHBoxLayout();
    lbar->addWidget(Sagiri::makeButton(lw, tr("Actualizar"), false, [this]() { loadLog(); }, "check"));
    lbar->addStretch(1);
    lv->addLayout(lbar);
    m_log = new QPlainTextEdit(lw);
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    lv->addWidget(m_log, 1);
    m_stack->addWidget(lw);

    // ---- Compartir
    auto* sw = new QWidget();
    auto* sv = new QVBoxLayout(sw);
    sv->setAlignment(Qt::AlignTop);
    sv->addWidget(new QLabel(tr("<h3>Compartir</h3><p>Para pasar esta instancia a otra computadora, copia su carpeta completa.</p>")));
    m_path = new QLabel(sw);
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sv->addWidget(m_path);
    sv->addWidget(Sagiri::makeButton(sw, tr("Abrir la carpeta"), true, [this]() {
        if (auto* inst = APPLICATION->instances()->getInstanceById(m_id))
            QDesktopServices::openUrl(QUrl::fromLocalFile(inst->instanceRoot()));
    }, "folder"));
    m_stack->addWidget(sw);

    v->addWidget(m_stack, 1);
}

void InstancePage::setInstance(const QString& id)
{
    m_id = id;
    auto* inst = APPLICATION->instances()->getInstanceById(id);
    if (!inst)
        return;
    m_icon->setPixmap(Sagiri::roundedPixmap(APPLICATION->icons()->getIcon(inst->iconKey()).pixmap(84, 84), 84, 84, 18));
    m_name->setText(QString("<h1>%1</h1>").arg(inst->name().toHtmlEscaped()));
    QString mc;
    const QString loader = loaderOf(inst, &mc);
    QStringList meta;
    meta << ((loader.isEmpty() ? QString() : loader + " ") + mc).trimmed();
    const qint64 secs = qint64(inst->totalTimePlayed());
    meta << (secs >= 3600 ? tr("%1 h jugadas").arg(secs / 3600) : tr("%1 min jugados").arg(secs / 60));
    const qint64 last = inst->lastLaunch();
    if (last > 0) {
        const qint64 days = (QDateTime::currentMSecsSinceEpoch() - last) / 86400000;
        meta << (days <= 0 ? tr("Ultima vez jugado hoy") : tr("Ultima vez jugado hace %1 dias").arg(days));
    } else {
        meta << tr("Todavia no la has jugado");
    }
    m_meta->setText(meta.join("   -   "));
    m_path->setText(inst->instanceRoot());
    reloadContent();
    loadWorlds();
}

void InstancePage::addRow(const QString& path, const QString& kind)
{
    const QFileInfo fi(path);
    QString base = fi.fileName();
    base.remove(QRegularExpression("\\.(jar|zip)(\\.disabled)?$", QRegularExpression::CaseInsensitiveOption));
    auto* f = new QFrame(m_rowsHost);
    f->setObjectName("card");
    f->setProperty("path", path);
    f->setProperty("kind", kind);
    f->setProperty("name", base.toLower());
    auto* h = new QHBoxLayout(f);
    h->setContentsMargins(14, 10, 14, 10);
    h->setSpacing(14);
    auto* ic = new QLabel(f);
    ic->setObjectName("rowIcon");
    ic->setFixedSize(48, 48);
    ic->setAlignment(Qt::AlignCenter);
    ic->setPixmap(Sagiri::icon(kind == "mod" ? "box" : (kind == "res" ? "image" : "palette"), QColor(255, 255, 255, 150), 30).pixmap(30, 30));
    ic->setStyleSheet("background:rgba(255,255,255,0.07);border-radius:10px;");
    h->addWidget(ic);
    auto* tv = new QVBoxLayout();
    auto* t = new QLabel(QString("<b>%1</b>").arg(base.toHtmlEscaped()), f);
    t->setObjectName("rowTitle");
    auto* s = new QLabel(kindLabel(kind), f);
    s->setObjectName("rowSub");
    s->setStyleSheet("color:#9aa7c7;");
    tv->addWidget(t);
    tv->addWidget(s);
    h->addLayout(tv, 1);
    auto* ver = new QLabel(QString("<span style='color:#9aa7c7'>%1</span>").arg(fi.fileName().toHtmlEscaped()), f);
    ver->setObjectName("rowVer");
    ver->setFixedWidth(300);
    h->addWidget(ver);
    if (kind == "mod") {
        auto* tg = new QCheckBox(f);
        tg->setProperty("switch", true);
        tg->setToolTip(tr("Activar o desactivar el mod"));
        tg->setChecked(!path.endsWith(".disabled", Qt::CaseInsensitive));
        connect(tg, &QCheckBox::toggled, this, [this, f](bool on) { toggleMod(f, on); });
        h->addWidget(tg);
    }
    h->addWidget(Sagiri::makeButton(f, QString(), false, [this, f]() { removeRow(f); }, "trash"));
    m_rows->insertWidget(m_rows->count() - 1, f);
    m_frames.append(f);
}

void InstancePage::reloadContent()
{
    ++m_token;
    for (auto* f : m_frames) {
        f->hide();
        f->deleteLater();
    }
    m_frames.clear();
    auto* inst = APPLICATION->instances()->getInstanceById(m_id);
    if (!inst)
        return;
    struct Src {
        QString dir;
        QString kind;
    };
    const Src srcs[3] = { { inst->modsRoot(), "mod" }, { inst->resourcePacksDir(), "res" }, { inst->shaderPacksDir(), "shd" } };
    QList<QPair<QString, QString>> files;
    for (const auto& s : srcs) {
        const QDir d(s.dir);
        for (const QFileInfo& fi : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
            if (s.kind == "mod" && (fi.isDir() || !(fi.fileName().endsWith(".jar", Qt::CaseInsensitive) || fi.fileName().endsWith(".jar.disabled", Qt::CaseInsensitive))))
                continue;
            files.append({ fi.absoluteFilePath(), s.kind });
        }
    }
    std::sort(files.begin(), files.end(), [](const QPair<QString, QString>& a, const QPair<QString, QString>& b) {
        return QFileInfo(a.first).fileName().toLower() < QFileInfo(b.first).fileName().toLower();
    });
    for (const auto& f : files)
        addRow(f.first, f.second);
    applyFilter();
    lookupHashes();
}

void InstancePage::applyFilter()
{
    static const char* kinds[4] = { "", "mod", "res", "shd" };
    const QString text = m_search->text().trimmed().toLower();
    int shown = 0;
    for (auto* f : m_frames) {
        const bool okKind = m_filter == 0 || f->property("kind").toString() == QString::fromLatin1(kinds[m_filter]);
        const QString title = f->findChild<QLabel*>("rowTitle") ? f->findChild<QLabel*>("rowTitle")->text().toLower() : QString();
        const bool okText = text.isEmpty() || f->property("name").toString().contains(text) || title.contains(text);
        f->setVisible(okKind && okText);
        if (okKind && okText)
            ++shown;
    }
    m_count->setText(tr("%1 proyectos").arg(shown));
}

void InstancePage::lookupHashes()
{
    QStringList paths;
    for (auto* f : m_frames) {
        const QString p = f->property("path").toString();
        if (QFileInfo(p).isFile())
            paths << p;
    }
    if (paths.isEmpty())
        return;
    const int token = m_token;
    auto* watcher = new QFutureWatcher<QMap<QString, QString>>(this);
    connect(watcher, &QFutureWatcher<QMap<QString, QString>>::finished, this, [this, watcher, token]() {
        const QMap<QString, QString> hashes = watcher->result();
        watcher->deleteLater();
        if (token == m_token)
            applyHashes(hashes, token);
    });
    watcher->setFuture(QtConcurrent::run([paths]() {
        QMap<QString, QString> m;
        for (const QString& p : paths) {
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            QCryptographicHash h(QCryptographicHash::Sha1);
            h.addData(&f);
            m.insert(p, QString::fromLatin1(h.result().toHex()));
        }
        return m;
    }));
}

void InstancePage::applyHashes(const QMap<QString, QString>& hashes, int token)
{
    QJsonArray arr;
    for (auto* f : m_frames) {
        const QString p = f->property("path").toString();
        if (hashes.contains(p)) {
            f->setProperty("sha1", hashes.value(p));
            arr.append(hashes.value(p));
        }
    }
    if (arr.isEmpty())
        return;
    QJsonObject body;
    body["hashes"] = arr;
    body["algorithm"] = "sha1";
    Sagiri::Http::get().post(QUrl("https://api.modrinth.com/v2/version_files"), QJsonDocument(body).toJson(QJsonDocument::Compact), this, [this, token](const QJsonDocument& d) {
        if (token != m_token)
            return;
        const QJsonObject map = d.object();
        QJsonArray ids;
        QHash<QFrame*, QString> projectOf;
        for (auto* f : m_frames) {
            const QString sha = f->property("sha1").toString();
            if (!map.contains(sha))
                continue;
            const QJsonObject ver = map.value(sha).toObject();
            const QString pid = ver.value("project_id").toString();
            projectOf.insert(f, pid);
            if (auto* l = f->findChild<QLabel*>("rowVer"))
                l->setText(QString("<b>%1</b><br><span style='color:#9aa7c7'>%2</span>")
                               .arg(ver.value("version_number").toString().toHtmlEscaped(), QFileInfo(f->property("path").toString()).fileName().toHtmlEscaped()));
            if (!ids.contains(pid))
                ids.append(pid);
        }
        if (ids.isEmpty())
            return;
        QUrl url("https://api.modrinth.com/v2/projects");
        QUrlQuery q;
        q.addQueryItem("ids", QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact)));
        url.setQuery(q);
        Sagiri::Http::get().json(url, this, [this, token, projectOf](const QJsonDocument& pd) {
            if (token != m_token)
                return;
            QHash<QString, QJsonObject> projects;
            QJsonArray teamIds;
            QHash<QString, QString> teamOfProject;
            for (const auto& pv : pd.array()) {
                const QJsonObject p = pv.toObject();
                projects.insert(p.value("id").toString(), p);
                teamOfProject.insert(p.value("id").toString(), p.value("team").toString());
                if (!teamIds.contains(p.value("team").toString()))
                    teamIds.append(p.value("team").toString());
            }
            for (auto it = projectOf.begin(); it != projectOf.end(); ++it) {
                QFrame* f = it.key();
                const QJsonObject p = projects.value(it.value());
                if (p.isEmpty())
                    continue;
                if (auto* t = f->findChild<QLabel*>("rowTitle"))
                    t->setText(QString("<b>%1</b>").arg(p.value("title").toString().toHtmlEscaped()));
                f->setProperty("name", p.value("title").toString().toLower());
                const QString icon = p.value("icon_url").toString();
                if (!icon.isEmpty()) {
                    if (auto* ic = f->findChild<QLabel*>("rowIcon"))
                        Sagiri::Http::get().pixmap(QUrl(icon), ic, [ic](const QPixmap& pm) { ic->setPixmap(Sagiri::roundedPixmap(pm, 48, 48, 10)); });
                }
            }
            // autores (dueno del equipo de cada proyecto)
            QUrl turl("https://api.modrinth.com/v2/teams");
            QUrlQuery tq;
            tq.addQueryItem("ids", QString::fromUtf8(QJsonDocument(teamIds).toJson(QJsonDocument::Compact)));
            turl.setQuery(tq);
            Sagiri::Http::get().json(turl, this, [this, token, projectOf, teamOfProject, teamIds](const QJsonDocument& td) {
                if (token != m_token)
                    return;
                const QJsonArray teams = td.array();
                QHash<QString, QString> ownerOf;
                for (int i = 0; i < teams.size() && i < teamIds.size(); ++i) {
                    QString name;
                    for (const auto& mv : teams.at(i).toArray()) {
                        const QJsonObject m = mv.toObject();
                        const QString u = m.value("user").toObject().value("username").toString();
                        if (name.isEmpty() || m.value("role").toString() == "Owner")
                            name = u;
                    }
                    ownerOf.insert(teamIds.at(i).toString(), name);
                }
                for (auto it = projectOf.begin(); it != projectOf.end(); ++it) {
                    const QString owner = ownerOf.value(teamOfProject.value(it.value()));
                    if (!owner.isEmpty()) {
                        if (auto* s = it.key()->findChild<QLabel*>("rowSub"))
                            s->setText(tr("por %1").arg(owner));
                    }
                }
            });
        });
    });
}

void InstancePage::toggleMod(QFrame* row, bool on)
{
    const QString path = row->property("path").toString();
    QString target = path;
    if (on)
        target.remove(QRegularExpression("\\.disabled$", QRegularExpression::CaseInsensitiveOption));
    else if (!path.endsWith(".disabled", Qt::CaseInsensitive))
        target += ".disabled";
    if (target != path && QFile::rename(path, target))
        row->setProperty("path", target);
}

void InstancePage::removeRow(QFrame* row)
{
    const QString path = row->property("path").toString();
    if (!QFile::moveToTrash(path)) {
        if (QFileInfo(path).isDir())
            QDir(path).removeRecursively();
        else
            QFile::remove(path);
    }
    m_frames.removeAll(row);
    row->hide();
    row->deleteLater();
    applyFilter();
    emit toast(tr("Movido a la papelera."));
}

void InstancePage::loadWorlds()
{
    m_worlds->clear();
    auto* inst = APPLICATION->instances()->getInstanceById(m_id);
    if (!inst)
        return;
    const QDir saves(QDir(inst->gameRoot()).filePath("saves"));
    for (const QFileInfo& fi : saves.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
        m_worlds->addItem(Sagiri::icon("store", Qt::white, 20), fi.fileName() + "    (" + fi.lastModified().toString("dd/MM/yyyy HH:mm") + ")");
    }
    if (m_worlds->count() == 0)
        m_worlds->addItem(tr("Todavia no hay mundos."));
}

void InstancePage::loadLog()
{
    auto* inst = APPLICATION->instances()->getInstanceById(m_id);
    if (!inst)
        return;
    QFile f(QDir(inst->gameRoot()).filePath("logs/latest.log"));
    if (!f.open(QIODevice::ReadOnly)) {
        m_log->setPlainText(tr("Todavia no hay registros: abre el juego una vez."));
        return;
    }
    const QList<QByteArray> lines = f.readAll().split('\n');
    QStringList tail;
    for (qsizetype i = qMax<qsizetype>(0, lines.size() - 400); i < lines.size(); ++i)
        tail << QString::fromUtf8(lines.at(i));
    m_log->setPlainText(tail.join(QLatin1Char('\n')));
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

// =================================================================== SkinsPage
SkinsPage::SkinsPage(QWidget* parent) : QWidget(parent)
{
    setAcceptDrops(true);
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(24);

    // izquierda: nombre, vista 3D y botones
    auto* left = new QVBoxLayout();
    left->addWidget(new QLabel(tr("<h1>Selector de skin</h1>")));
    m_tag = new QLabel(this);
    m_tag->setAlignment(Qt::AlignCenter);
    m_tag->setStyleSheet("background:rgba(0,0,0,0.35);border-radius:8px;padding:6px 14px;font-weight:700;font-family:Consolas,monospace;font-size:16px;");
    left->addWidget(m_tag, 0, Qt::AlignHCenter);
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
    left->addWidget(previewWidget, 1);
    auto* drag = new QLabel(tr("Arrastra para girar"), this);
    drag->setAlignment(Qt::AlignCenter);
    drag->setStyleSheet("color:#9aa7c7;");
    left->addWidget(drag);
    left->addWidget(Sagiri::makeButton(this, tr("Usar esta skin"), true, [this]() { apply(); }, "check"));
    left->addWidget(Sagiri::makeButton(this, tr("Editar skin"), false, [this]() { editSelected(); }, "edit"));
    left->addWidget(Sagiri::makeButton(this, tr("Eliminar"), false, [this]() { removeSelected(); }, "trash"));
    auto* leftW = new QWidget(this);
    leftW->setLayout(left);
    leftW->setFixedWidth(340);
    h->addWidget(leftW);

    // derecha: skins guardadas
    auto* right = new QVBoxLayout();
    right->addWidget(new QLabel(tr("<h2>Skins guardadas</h2>")));
    m_info = new QLabel(this);
    m_info->setStyleSheet("color:#9aa7c7;");
    right->addWidget(m_info);
    m_gridHost = new QWidget();
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setSpacing(14);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(wrapScroll(m_gridHost), 1);
    h->addLayout(right, 1);
}

SkinsPage::~SkinsPage()
{
    delete m_preview;
    delete m_list;
}

void SkinsPage::reload()
{
    auto acct = APPLICATION->accounts()->defaultAccount();
    if (!acct) {
        m_acct = MinecraftAccountPtr();
        delete m_list;
        m_list = nullptr;
        m_tag->setText(tr("Sin cuenta"));
        m_info->setText(tr("Inicia sesion con una cuenta de Microsoft en la pagina Cuenta para ver y cambiar tus skins."));
        Sagiri::clearLayout(m_grid);
        return;
    }
    m_info->clear();
    if (!m_list || m_acct.get() != acct.get()) {
        delete m_list;
        m_acct = acct;
        m_list = new SkinList(this, APPLICATION->settings()->get("SkinsDir").toString(), acct);
        m_list->update();
        const int row = m_list->getSelectedAccountSkin();
        if (row >= 0)
            m_selected = m_list->data(m_list->index(row), Qt::UserRole).toString();
    }
    m_tag->setText(m_acct->profileName());
    rebuild();
    select(m_selected);
}

void SkinsPage::rebuild()
{
    Sagiri::clearLayout(m_grid);
    if (!m_list)
        return;
    int n = 0;
    // tarjeta "Anadir skin"
    auto* add = new Sagiri::LinkCard(m_gridHost);
    add->setFixedSize(200, 330);
    add->setStyleSheet("QFrame{border:1px dashed rgba(255,255,255,0.35);border-radius:16px;background:transparent;}");
    auto* av = new QVBoxLayout(add);
    av->setAlignment(Qt::AlignCenter);
    auto* plus = new QLabel(add);
    plus->setPixmap(Sagiri::icon("plus", Qt::white, 36).pixmap(36, 36));
    plus->setAlignment(Qt::AlignCenter);
    plus->setStyleSheet("border:0;");
    auto* at = new QLabel(tr("<b>Anadir skin</b><br><span style='color:#9aa7c7'>Arrastra y suelta</span>"), add);
    at->setAlignment(Qt::AlignCenter);
    at->setStyleSheet("border:0;");
    av->addWidget(plus);
    av->addWidget(at);
    connect(add, &Sagiri::LinkCard::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(this, tr("Elegir skin"), QString(), tr("Skins (*.png)"));
        if (!f.isEmpty())
            addFile(f);
    });
    m_grid->addWidget(add, 0, 0);
    ++n;
    for (int r = 0; r < m_list->rowCount(); ++r) {
        const QString key = m_list->data(m_list->index(r), Qt::UserRole).toString();
        auto* skin = m_list->skin(key);
        if (!skin)
            continue;
        auto* c = new Sagiri::LinkCard(m_gridHost);
        c->setObjectName("card");
        c->setFixedSize(200, 330);
        if (key == m_selected)
            c->setStyleSheet("#card{border:2px solid #ffffff;}");
        auto* v = new QVBoxLayout(c);
        v->setContentsMargins(10, 12, 10, 12);
        auto* img = new QLabel(c);
        img->setAlignment(Qt::AlignCenter);
        img->setPixmap(frontView(skin->getTexture(), skin->getModel() == SkinModel::SLIM, 8));
        img->setStyleSheet("border:0;background:transparent;");
        v->addWidget(img, 1);
        auto* nm = new QLabel(skin->name(), c);
        nm->setAlignment(Qt::AlignCenter);
        nm->setStyleSheet("border:0;color:#9aa7c7;");
        v->addWidget(nm);
        if (key == m_selected) {
            auto* ck = new QLabel(c);
            QPixmap pm(28, 28);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(Qt::white);
            p.setPen(Qt::NoPen);
            p.drawEllipse(1, 1, 26, 26);
            p.setPen(QPen(QColor("#111111"), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(QPolygonF({ QPointF(8, 14), QPointF(12.5, 18.5), QPointF(20, 9) }));
            p.end();
            ck->setPixmap(pm);
            ck->setStyleSheet("border:0;background:transparent;");
            ck->move(160, 10);
            ck->show();
        }
        connect(c, &Sagiri::LinkCard::clicked, this, [this, key]() {
            select(key);
            rebuild();
        });
        m_grid->addWidget(c, n / 3, n % 3);
        ++n;
    }
}

void SkinsPage::select(const QString& key)
{
    m_selected = key;
    auto* skin = m_list ? m_list->skin(key) : nullptr;
    if (!skin)
        return;
    if (m_preview)
        m_preview->updateScene(skin);
    else if (m_previewLabel)
        m_previewLabel->setPixmap(frontView(skin->getTexture(), skin->getModel() == SkinModel::SLIM, 10));
}

void SkinsPage::addFile(const QString& file)
{
    if (!m_list)
        return;
    const QString msg = m_list->installSkin(file);
    if (!msg.isEmpty()) {
        emit toast(msg);
        return;
    }
    m_list->update();
    rebuild();
    emit toast(tr("Skin anadida."));
}

void SkinsPage::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void SkinsPage::dropEvent(QDropEvent* e)
{
    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile() && u.toLocalFile().endsWith(".png", Qt::CaseInsensitive))
            addFile(u.toLocalFile());
    }
}

void SkinsPage::editSelected()
{
    auto* skin = m_list ? m_list->skin(m_selected) : nullptr;
    if (!skin) {
        emit toast(tr("Elige primero una skin."));
        return;
    }
    SkinEditorDialog dlg(this, skin->getTexture(), skin->getModel() == SkinModel::SLIM, skin->name());
    if (dlg.exec() != QDialog::Accepted)
        return;
    QTemporaryDir tmp;
    const QString file = tmp.filePath("skin.png");
    if (!tmp.isValid() || !dlg.image().save(file, "PNG")) {
        emit toast(tr("No pude guardar la skin."));
        return;
    }
    QString name = dlg.skinName().trimmed();
    name.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    if (name.isEmpty())
        name = skin->name() + " editada";
    const QString msg = m_list->installSkin(file, name + ".png");
    if (!msg.isEmpty()) {
        emit toast(msg);
        return;
    }
    m_list->update();
    rebuild();
}

void SkinsPage::removeSelected()
{
    if (!m_list || m_selected.isEmpty())
        return;
    const auto r = CustomMessageBox::selectable(this, tr("Eliminar skin"), tr("Mover esta skin a la papelera?"), QMessageBox::Warning,
                                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                       ->exec();
    if (r != QMessageBox::Yes)
        return;
    m_list->deleteSkin(m_selected, true);
    m_selected.clear();
    m_list->update();
    rebuild();
}

void SkinsPage::apply()
{
    auto* skin = m_list ? m_list->skin(m_selected) : nullptr;
    if (!skin || !m_acct) {
        emit toast(tr("Elige primero una skin."));
        return;
    }
    const QString path = skin->getPath();
    if (!QFile::exists(path)) {
        emit toast(tr("No existe el archivo de la skin."));
        return;
    }
    NetJob::Ptr skinUpload{ new NetJob(tr("Cambiar skin"), APPLICATION->network(), 1) };
    skinUpload->addNetAction(makeSkinUploadRequest(m_acct->accessToken(), path, skin->getModelString()));
    const auto selectedCape = skin->getCapeId();
    if (selectedCape != m_acct->accountData()->minecraftProfile.currentCape)
        skinUpload->addNetAction(makeCapeChangeRequest(m_acct->accessToken(), selectedCape));
    skinUpload->addTask(m_acct->refresh().staticCast<Task>());
    ProgressDialog prog(this);
    if (prog.execWithTask(skinUpload.get()) != QDialog::Accepted) {
        emit toast(tr("No se pudo cambiar la skin."));
        return;
    }
    skin->setURL(m_acct->accountData()->minecraftProfile.skin.url);
    emit toast(tr("Skin aplicada a tu cuenta."));
}

// ======================================================== CreateInstanceDialog
CreateInstanceDialog::CreateInstanceDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Crear instancia"));
    resize(540, 440);
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(tr("<h2>Crear instancia</h2><p>Elige la version de Minecraft y el cargador de mods que quieras.</p>")));
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("Nombre de la instancia"));
    m_mc = new QComboBox(this);
    m_snap = new QCheckBox(tr("Mostrar snapshots y versiones antiguas"), this);
    m_loader = new QComboBox(this);
    m_loader->addItem(tr("Vanilla (sin mods)"), QString());
    m_loader->addItem("Fabric", "net.fabricmc.fabric-loader");
    m_loader->addItem("Quilt", "org.quiltmc.quilt-loader");
    m_loader->addItem("Forge", "net.minecraftforge");
    m_loader->addItem("NeoForge", "net.neoforged");
    m_lver = new QComboBox(this);
    m_lver->setEnabled(false);
    m_status = new QLabel(this);
    m_status->setStyleSheet("color:#ff8a8a;");
    auto* g = new QGridLayout();
    g->addWidget(new QLabel(tr("Nombre"), this), 0, 0);
    g->addWidget(m_name, 0, 1);
    g->addWidget(new QLabel(tr("Version de Minecraft"), this), 1, 0);
    g->addWidget(m_mc, 1, 1);
    g->addWidget(m_snap, 2, 1);
    g->addWidget(new QLabel(tr("Cargador de mods"), this), 3, 0);
    g->addWidget(m_loader, 3, 1);
    g->addWidget(new QLabel(tr("Version del cargador"), this), 4, 0);
    g->addWidget(m_lver, 4, 1);
    g->setColumnStretch(1, 1);
    v->addLayout(g);
    v->addWidget(m_status);
    v->addStretch(1);
    auto* row = new QHBoxLayout();
    row->addStretch(1);
    row->addWidget(Sagiri::makeButton(this, tr("Cancelar"), false, [this]() { reject(); }));
    row->addWidget(Sagiri::makeButton(this, tr("Crear instancia"), true, [this]() { accept(); }, "plus"));
    v->addLayout(row);

    connect(m_snap, &QCheckBox::toggled, this, [this]() { fillMc(); });
    connect(m_mc, &QComboBox::activated, this, [this]() { loadLoader(); });
    connect(m_loader, &QComboBox::activated, this, [this]() { loadLoader(); });
    fetchIndex("net.minecraft", [this](const QJsonArray&) { fillMc(); });
}

void CreateInstanceDialog::fetchIndex(const QString& uid, const std::function<void(const QJsonArray&)>& cb)
{
    if (m_idx.contains(uid)) {
        cb(m_idx.value(uid));
        return;
    }
    m_status->setText(tr("Cargando versiones..."));
    Sagiri::Http::get().json(QUrl("https://meta.prismlauncher.org/v1/" + uid + "/index.json"), this, [this, uid, cb](const QJsonDocument& d) {
        const QJsonArray versions = d.object().value("versions").toArray();
        if (versions.isEmpty()) {
            m_status->setText(tr("No pude cargar la lista de versiones (revisa tu internet)."));
            return;
        }
        m_status->clear();
        m_idx.insert(uid, versions);
        cb(versions);
    });
}

void CreateInstanceDialog::fillMc()
{
    m_mc->clear();
    for (const auto& vv : m_idx.value("net.minecraft")) {
        const QJsonObject v = vv.toObject();
        if (!m_snap->isChecked() && v.value("type").toString() != "release")
            continue;
        m_mc->addItem(v.value("version").toString(), v.value("version").toString());
    }
    loadLoader();
}

void CreateInstanceDialog::loadLoader()
{
    m_lver->clear();
    const QString uid = m_loader->currentData().toString();
    if (uid.isEmpty()) {
        m_lver->setEnabled(false);
        return;
    }
    const QString mc = m_mc->currentData().toString();
    fetchIndex(uid, [this, mc](const QJsonArray& versions) {
        m_lver->clear();
        for (const auto& vv : versions) {
            const QJsonObject v = vv.toObject();
            if (!compatibleWith(v, mc))
                continue;
            const QString ver = v.value("version").toString();
            m_lver->addItem(v.value("recommended").toBool() ? ver + tr(" (recomendada)") : ver, ver);
        }
        m_lver->setEnabled(m_lver->count() > 0);
        if (m_lver->count() == 0)
            m_status->setText(tr("Ese cargador no tiene version para Minecraft %1.").arg(mc));
        else
            m_status->clear();
    });
}

void CreateInstanceDialog::accept()
{
    const QString name = m_name->text().trimmed();
    const QString mc = m_mc->currentData().toString();
    const QString uid = m_loader->currentData().toString();
    const QString lv = m_lver->currentData().toString();
    if (name.isEmpty() || mc.isEmpty()) {
        m_status->setText(tr("Escribe un nombre y elige una version de Minecraft."));
        return;
    }
    if (!uid.isEmpty() && lv.isEmpty()) {
        m_status->setText(tr("Elige una version del cargador."));
        return;
    }
    QString base = APPLICATION->settings()->get("InstanceDir").toString();
    if (QDir::isRelativePath(base))
        base = QDir(APPLICATION->dataRoot()).absoluteFilePath(base);
    const QString folder = slugOf(name);
    QString full = QDir(base).filePath(folder);
    int n = 2;
    while (QFileInfo::exists(full))
        full = QDir(base).filePath(folder + "-" + QString::number(n++));
    if (!QDir().mkpath(QDir(full).filePath("minecraft"))) {
        m_status->setText(tr("No pude crear la carpeta de la instancia."));
        return;
    }
    QJsonArray comps;
    QJsonObject mcc;
    mcc["important"] = true;
    mcc["uid"] = "net.minecraft";
    mcc["version"] = mc;
    comps.append(mcc);
    if (!uid.isEmpty()) {
        if (uid == "net.fabricmc.fabric-loader" || uid == "org.quiltmc.quilt-loader") {
            QJsonObject inter;
            inter["uid"] = "net.fabricmc.intermediary";
            inter["version"] = mc;
            inter["dependencyOnly"] = true;
            comps.append(inter);
        }
        QJsonObject lc;
        lc["uid"] = uid;
        lc["version"] = lv;
        comps.append(lc);
    }
    QJsonObject root;
    root["components"] = comps;
    root["formatVersion"] = 1;
    QFile pack(QDir(full).filePath("mmc-pack.json"));
    if (pack.open(QIODevice::WriteOnly)) {
        pack.write(QJsonDocument(root).toJson());
        pack.close();
    }
    QFile cfg(QDir(full).filePath("instance.cfg"));
    if (cfg.open(QIODevice::WriteOnly | QIODevice::Text)) {
        cfg.write(QString("[General]\nInstanceType=OneSix\niconKey=default\nname=%1\n").arg(name).toUtf8());
        cfg.close();
    }
    APPLICATION->instances()->loadList();
    QDialog::accept();
}
