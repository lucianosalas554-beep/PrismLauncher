// SPDX-License-Identifier: GPL-3.0-only
// Editor de skins de SagiriPrism (estilo Blockbench): barra de herramientas, vista 3D en vivo,
// textura con zoom, color, pincel y capas. Sin acentos a proposito.
#pragma once

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include "minecraft/skins/SkinModel.h"
#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"

class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

class PixelCanvas : public QWidget {
    Q_OBJECT
   public:
    enum class Tool { Pencil, Eraser, Fill, Picker };

    explicit PixelCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    QImage image() const { return m_img; }
    QImage visibleImage() const;
    void setTool(Tool t) { m_tool = t; }
    void setColor(const QColor& c) { m_color = c; }
    void setBrush(int n) { m_brush = qBound(1, n, 6); }
    void setZoom(int z);
    void setShowGrid(bool v);
    void setShowGuide(bool v);
    void setRegionVisible(int idx, bool v);
    static int regionCount();
    static QString regionName(int idx);
    void undo();
    void redo();

   signals:
    void changed();
    void colorPicked(const QColor& color);
    void zoomWheel(int direction);

   protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

   private:
    QPoint pixelAt(const QPoint& pos) const;
    int regionAt(const QPoint& p) const;
    void paintPixel(const QPoint& p);
    void drawLine(QPoint a, QPoint b);
    void fill(const QPoint& p);
    void pick(const QPoint& p);
    void pushUndo();

    QImage m_img;
    QList<QImage> m_undo;
    QList<QImage> m_redo;
    Tool m_tool = Tool::Pencil;
    QColor m_color = QColor(0xe8, 0xa3, 0x17);
    int m_brush = 1;
    int m_zoom = 8;
    bool m_grid = true;
    bool m_guide = true;
    bool m_drawing = false;
    bool m_hidden[12] = { false, false, false, false, false, false, false, false, false, false, false, false };
    QPoint m_last;
};

class SkinEditorDialog : public QDialog, public SkinProvider {
    Q_OBJECT
   public:
    SkinEditorDialog(QWidget* parent, const QImage& skin, bool slim, const QString& name);
    ~SkinEditorDialog() override;

    QImage image() const;
    QString skinName() const;

    SkinModel* getSelectedSkin() override { return &m_model; }
    QHash<QString, QImage> capes() override { return {}; }

   private slots:
    void refreshPreview();

   private:
    void setTool(int index);
    void setColor(const QColor& c, bool addRecent);
    void rebuildPalette();

    PixelCanvas* m_canvas = nullptr;
    SkinOpenGLWindow* m_preview = nullptr;
    QLabel* m_previewLabel = nullptr;
    QPushButton* m_colorBtn = nullptr;
    QLineEdit* m_hex = nullptr;
    QGridLayout* m_palLayout = nullptr;
    QLineEdit* m_name = nullptr;
    QToolButton* m_toolBtns[4] = { nullptr, nullptr, nullptr, nullptr };
    QTemporaryDir m_tmp;
    QString m_tmpPath;
    SkinModel m_model;
    QTimer m_timer;
    QList<QColor> m_recent;
};
