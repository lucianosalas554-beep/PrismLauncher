// SPDX-License-Identifier: GPL-3.0-only
// Ventana principal de SagiriPrism: barra lateral, carrusel, explorador estilo Modrinth, videos, cuenta y ajustes.
// Usa el motor de Prism (instancias, cuentas, lanzamiento). Sin acentos a proposito.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QPixmap>
#include <QPointer>
#include <QRectF>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>
#include <functional>

class InstancePage;
class MainWindow;
class SkinsPage;
class QButtonGroup;
class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

class BloqueCarousel : public QWidget {
    Q_OBJECT
   public:
    struct Item {
        QString id, name, sub;
        QPixmap icon;
        int hue = 0;
    };
    explicit BloqueCarousel(QWidget* parent = nullptr);
    void setItems(const QList<Item>& items);
    void select(const QString& id);
    QString currentId() const;

   signals:
    void playRequested(const QString& id);
    void editRequested(const QString& id);
    void currentChanged(const QString& id);

   protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

   private:
    void goTo(int i);
    void layoutButtons();

    QList<Item> m_items;
    int m_cur = 0;
    double m_pos = 0;
    QVariantAnimation m_anim;
    QPushButton* m_play = nullptr;
    QPushButton* m_edit = nullptr;
    QPushButton* m_prev = nullptr;
    QPushButton* m_next = nullptr;
    QList<QPair<int, QRectF>> m_hit;
};

class BloqueWindow : public QMainWindow {
    Q_OBJECT
   public:
    explicit BloqueWindow(MainWindow* classicWindow);

   protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

   private:
    QWidget* makeHome();
    QWidget* makeInstances();
    QWidget* makeExplore();
    QWidget* makeVideos();
    QWidget* makeAccount();
    QWidget* makeSettings();

    void go(int page);
    void applyTheme();
    void refresh();
    void updateDetail();
    void updateAccount();
    void toast(const QString& text);
    QPixmap avatar(int px) const;
    QString currentInstanceId() const;

    void launch(const QString& id);
    void editInstance(const QString& id);
    void changeIcon(const QString& id);
    void deleteInstance(const QString& id);
    void openFolder(const QString& id);
    void openSkins();
    void createInstance();

    void modrinthSearch(const QString& type, const QString& query, int offset, const std::function<void(const QJsonArray&)>& cb);
    void loadPopular();
    void loadExplore(bool reset);
    void installModpack(const QString& projectId, const QString& title);
    void installProject(const QString& projectId, const QString& type, const QString& instId, const QString& title);
    void fetchVersion(const QString& projectId, const QString& type, const QString& mc, const QString& loader, bool filtered,
                      const std::function<void(const QJsonObject&)>& cb);
    void downloadVersionFile(const QJsonObject& version, const QString& dir, const std::function<void(bool, const QString&)>& done);
    void loadFeed(int which);

    QPointer<MainWindow> m_classic;
    QStackedWidget* m_pages = nullptr;
    QButtonGroup* m_nav = nullptr;
    QLineEdit* m_search = nullptr;
    QPushButton* m_chip = nullptr;
    QLabel* m_logo = nullptr;
    BloqueCarousel* m_carousel = nullptr;
    InstancePage* m_instPage = nullptr;
    SkinsPage* m_skins = nullptr;
    QListWidget* m_quick = nullptr;
    QListWidget* m_grid = nullptr;
    QHBoxLayout* m_popularRow = nullptr;
    QWidget* m_detPanel = nullptr;
    QLabel* m_detIcon = nullptr;
    QLabel* m_detName = nullptr;
    QLabel* m_detSub = nullptr;

    QString m_exType = "modpack";
    QLineEdit* m_exSearch = nullptr;
    QComboBox* m_exInst = nullptr;
    QGridLayout* m_exGrid = nullptr;
    QWidget* m_exHost = nullptr;
    QPushButton* m_exMore = nullptr;
    QTimer m_exTimer;
    int m_exOffset = 0;
    int m_exCount = 0;
    int m_exToken = 0;

    QButtonGroup* m_vidTabs = nullptr;
    QStackedWidget* m_vidStack = nullptr;
    QGridLayout* m_vidGrid[3] = { nullptr, nullptr, nullptr };
    bool m_vidLoaded[3] = { false, false, false };

    QLabel* m_accAvatar = nullptr;
    QLabel* m_accName = nullptr;
    QListWidget* m_accounts = nullptr;
    QString m_filter;
};
