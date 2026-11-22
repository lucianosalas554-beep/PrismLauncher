// SPDX-License-Identifier: GPL-3.0-only
// Paginas de SagiriPrism: instancia (estilo Modrinth), selector de skins y creacion de instancias.
// Sin acentos a proposito.
#pragma once

#include <QDialog>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QWidget>
#include <functional>

#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/skins/SkinList.h"
#include "minecraft/skins/SkinModel.h"
#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

// Pagina de una instancia: Contenido, Archivos, Mundos, Registros y Compartir.
class InstancePage : public QWidget {
    Q_OBJECT
   public:
    explicit InstancePage(QWidget* parent = nullptr);
    void setInstance(const QString& id);

   signals:
    void backRequested();
    void playRequested(const QString& id);
    void exploreRequested(const QString& id);
    void advancedRequested(const QString& id);
    void toast(const QString& text);

   private:
    void reloadContent();
    void addRow(const QString& path, const QString& kind);
    void applyFilter();
    void lookupHashes();
    void applyHashes(const QMap<QString, QString>& hashes, int token);
    void toggleMod(QFrame* row, bool on);
    void removeRow(QFrame* row);
    void loadWorlds();
    void loadLog();

    QString m_id;
    QLabel* m_icon = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_meta = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_count = nullptr;
    QWidget* m_rowsHost = nullptr;
    QVBoxLayout* m_rows = nullptr;
    QList<QFrame*> m_frames;
    int m_filter = 0;
    int m_token = 0;
    QListWidget* m_worlds = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QLabel* m_path = nullptr;
    QStackedWidget* m_stack = nullptr;
};

// Selector de skins: vista 3D a la izquierda y tus skins guardadas a la derecha.
class SkinsPage : public QWidget, public SkinProvider {
    Q_OBJECT
   public:
    explicit SkinsPage(QWidget* parent = nullptr);
    ~SkinsPage() override;
    void reload();

    SkinModel* getSelectedSkin() override { return m_list ? m_list->skin(m_selected) : nullptr; }
    QHash<QString, QImage> capes() override { return {}; }

   signals:
    void toast(const QString& text);

   protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

   private:
    void rebuild();
    void select(const QString& key);
    void apply();
    void addFile(const QString& file);
    void editSelected();
    void removeSelected();

    MinecraftAccountPtr m_acct;
    SkinList* m_list = nullptr;
    QString m_selected;
    QLabel* m_tag = nullptr;
    SkinOpenGLWindow* m_preview = nullptr;
    QLabel* m_previewLabel = nullptr;
    QWidget* m_gridHost = nullptr;
    QGridLayout* m_grid = nullptr;
    QLabel* m_info = nullptr;
};

// Crear una instancia eligiendo version de Minecraft y cargador de mods.
class CreateInstanceDialog : public QDialog {
    Q_OBJECT
   public:
    explicit CreateInstanceDialog(QWidget* parent);
    void accept() override;

   private:
    void fetchIndex(const QString& uid, const std::function<void(const QJsonArray&)>& cb);
    void fillMc();
    void loadLoader();

    QLineEdit* m_name = nullptr;
    QComboBox* m_mc = nullptr;
    QComboBox* m_loader = nullptr;
    QComboBox* m_lver = nullptr;
    QCheckBox* m_snap = nullptr;
    QLabel* m_status = nullptr;
    QHash<QString, QJsonArray> m_idx;
};
