// SPDX-License-Identifier: GPL-3.0-only
// Editor de skins para Bloque (basado en Prism Launcher). Codigo nuevo, sin acentos a proposito
// para compilar igual con GCC, Clang y MSVC.
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

class QCheckBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;

// Lienzo 64x64 con lapiz, borrador, relleno, gotero, cuadricula, guia de partes y deshacer/rehacer.
class PixelCanvas : public QWidget {
    Q_OBJECT
   public:
    enum class Tool { Pencil, Eraser, Fill, Picker };

    explicit PixelCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    QImage image() const { return m_img; }
    void setTool(Tool t) { m_tool = t; }
    void setColor(const QColor& c) { m_color = c; }
    void setShowGrid(bool v);
    void setShowGuide(bool v);
    void undo();
    void redo();
    QSize sizeHint() const override { return { 512, 512 }; }

   signals:
    void changed();
    void colorPicked(const QColor& color);

   protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

   private:
    QRect target() const;
    QPoint pixelAt(const QPoint& pos) const;
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
    bool m_grid = true;
    bool m_guide = true;
    bool m_drawing = false;
    QPoint m_last;
};

class SkinEditorDialog : public QDialog, public SkinProvider {
    Q_OBJECT
   public:
    SkinEditorDialog(QWidget* parent, const QImage& skin, bool slim, const QString& name);
    ~SkinEditorDialog() override;

    QImage image() const;
    QString skinName() const;

    // SkinProvider: la vista previa 3D pide aqui la skin que se esta editando
    SkinModel* getSelectedSkin() override { return &m_model; }
    QHash<QString, QImage> capes() override { return {}; }

   private slots:
    void refreshPreview();

   private:
    void setColor(const QColor& c, bool addRecent);
    void rebuildPalette();

    PixelCanvas* m_canvas = nullptr;
    SkinOpenGLWindow* m_preview = nullptr;
    QLabel* m_previewLabel = nullptr;
    QPushButton* m_colorBtn = nullptr;
    QGridLayout* m_palLayout = nullptr;
    QLineEdit* m_name = nullptr;
    QTemporaryDir m_tmp;
    QString m_tmpPath;
    SkinModel m_model;
    QTimer m_timer;
    QList<QColor> m_recent;
};
