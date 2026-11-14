// SPDX-License-Identifier: GPL-3.0-only
// Ventana principal de Bloque: barra lateral, carrusel de instancias, descargas, videos y mas.
// Usa el motor de Prism (instancias, cuentas, lanzamiento y sus dialogos). Sin acentos a proposito.
#pragma once

#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QPixmap>
#include <QPointer>
#include <QRectF>
#include <QVariantAnimation>
#include <QWidget>

class MainWindow;
class QButtonGroup;
class QFrame;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

// Carrusel de instancias: la tarjeta central grande, las laterales mas pequenas y oscuras.
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

   private:
    QWidget* makeHome();
    QWidget* makeInstances();
    QWidget* makeDownload();
    QWidget* makeVideos();
    QWidget* makeAccount();
    QWidget* makeSettings();

    void go(int page);
    void refresh();
    void updateAccount();
    void classic(const char* slot);
    void launch(const QString& id);
    void editInstance(const QString& id);
    void openSkins();
    QString currentInstanceId() const;

    QPointer<MainWindow> m_classic;
    QStackedWidget* m_pages = nullptr;
    QButtonGroup* m_nav = nullptr;
    QLineEdit* m_search = nullptr;
    QPushButton* m_chip = nullptr;
    BloqueCarousel* m_carousel = nullptr;
    QListWidget* m_quick = nullptr;
    QListWidget* m_grid = nullptr;
    QString m_filter;
};
