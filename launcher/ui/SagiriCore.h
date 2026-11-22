// SPDX-License-Identifier: GPL-3.0-only
// SagiriPrism: configuracion, temas, iconos blancos, sonidos y acceso a internet con imagenes.
#pragma once

#include <QColor>
#include <QFrame>
#include <QHash>
#include <QIcon>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QLayout>
#include <QObject>
#include <QPushButton>
#include <QPixmap>
#include <QSettings>
#include <QStringList>
#include <QUrl>
#include <functional>

namespace Sagiri {

struct Theme {
    QString id, name;
    QColor bg, panel, panel2, text, muted, accent, onAccent;
    QString font;
    int radius = 14;
    bool glass = false;
};

class Config : public QObject {
    Q_OBJECT
   public:
    static Config& get();
    QString themeId() const;
    void setThemeId(const QString& id);
    Theme theme() const;
    QVariant value(const QString& key, const QVariant& def = QVariant()) const;
    void setValue(const QString& key, const QVariant& v);

   signals:
    void changed();

   private:
    Config();
    QSettings m_s;
};

QString buildStyleSheet(const Theme& t, bool translucent, int alphaPct, const QString& fontFamily, int fontSize);
QIcon icon(const QString& name, const QColor& color = Qt::white, int size = 22);
QPixmap roundedPixmap(const QPixmap& src, int w, int h, int radius);
void playSound(const QString& name);
QIcon appIcon();
QPushButton* makeButton(QWidget* parent, const QString& text, bool primary, const std::function<void()>& fn, const QString& iconName = QString());
void clearLayout(QLayout* l);

// Peticiones de red con resultado en una funcion. El objeto 'ctx' evita llamar si ya no existe.
class Http : public QObject {
    Q_OBJECT
   public:
    static Http& get();
    void bytes(const QUrl& url, QObject* ctx, const std::function<void(const QByteArray&)>& cb);
    void json(const QUrl& url, QObject* ctx, const std::function<void(const QJsonDocument&)>& cb);
    void pixmap(const QUrl& url, QObject* ctx, const std::function<void(const QPixmap&)>& cb);
    void post(const QUrl& url, const QByteArray& body, QObject* ctx, const std::function<void(const QJsonDocument&)>& cb);

   private:
    Http() = default;
    QNetworkAccessManager m_nam;
    QHash<QString, QPixmap> m_cache;
};

// Tarjeta en la que se puede hacer clic (videos, noticias).
class LinkCard : public QFrame {
    Q_OBJECT
   public:
    explicit LinkCard(QWidget* parent = nullptr) : QFrame(parent) { setCursor(Qt::PointingHandCursor); }

   signals:
    void clicked();

   protected:
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()))
            emit clicked();
        QFrame::mouseReleaseEvent(e);
    }
};

}  // namespace Sagiri
