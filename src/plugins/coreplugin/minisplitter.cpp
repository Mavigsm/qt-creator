// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "minisplitter.h"

#include "generalsettings.h"

#include <utils/stylehelper.h>
#include <utils/theme/theme.h>

#include <QApplication>
#include <QPaintEvent>
#include <QPainter>
#include <QSplitterHandle>

namespace Core {
namespace Internal {

static QBitmap scaledBitmap(const QBitmap &other, qreal factor)
{
    QTransform trans = QTransform::fromScale(factor, factor);
    return other.transformed(trans);
}

// cursor images / masks taken from qplatformcursor.cpp
static QCursor hsplitCursor(qreal ratio)
{
    static const uchar hsplit_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00,
        0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00,
        0x00, 0x41, 0x82, 0x00, 0x80, 0x41, 0x82, 0x01, 0xc0, 0x7f, 0xfe, 0x03,
        0x80, 0x41, 0x82, 0x01, 0x00, 0x41, 0x82, 0x00, 0x00, 0x40, 0x02, 0x00,
        0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00,
        0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uchar hsplitm_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xe0, 0x07, 0x00, 0x00, 0xe0, 0x07, 0x00, 0x00, 0xe0, 0x07, 0x00,
        0x00, 0xe0, 0x07, 0x00, 0x00, 0xe2, 0x47, 0x00, 0x00, 0xe3, 0xc7, 0x00,
        0x80, 0xe3, 0xc7, 0x01, 0xc0, 0xff, 0xff, 0x03, 0xe0, 0xff, 0xff, 0x07,
        0xc0, 0xff, 0xff, 0x03, 0x80, 0xe3, 0xc7, 0x01, 0x00, 0xe3, 0xc7, 0x00,
        0x00, 0xe2, 0x47, 0x00, 0x00, 0xe0, 0x07, 0x00, 0x00, 0xe0, 0x07, 0x00,
        0x00, 0xe0, 0x07, 0x00, 0x00, 0xe0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static QBitmap cursorImg = QBitmap::fromData({32, 32}, hsplit_bits);
    static QBitmap mask = QBitmap::fromData({32, 32}, hsplitm_bits);
    return QCursor(scaledBitmap(cursorImg, ratio), scaledBitmap(mask, ratio),
                   15 * ratio, 15 * ratio);
}

static QCursor vsplitCursor(qreal ratio)
{
    static const uchar vsplit_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0xe0, 0x03, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xff, 0x7f, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x7f, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xe0, 0x03, 0x00,
        0x00, 0xc0, 0x01, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uchar vsplitm_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0xc0, 0x01, 0x00, 0x00, 0xe0, 0x03, 0x00, 0x00, 0xf0, 0x07, 0x00,
        0x00, 0xf8, 0x0f, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0xc0, 0x01, 0x00,
        0x00, 0xc0, 0x01, 0x00, 0x80, 0xff, 0xff, 0x00, 0x80, 0xff, 0xff, 0x00,
        0x80, 0xff, 0xff, 0x00, 0x80, 0xff, 0xff, 0x00, 0x80, 0xff, 0xff, 0x00,
        0x80, 0xff, 0xff, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0xc0, 0x01, 0x00,
        0x00, 0xc0, 0x01, 0x00, 0x00, 0xf8, 0x0f, 0x00, 0x00, 0xf0, 0x07, 0x00,
        0x00, 0xe0, 0x03, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static QBitmap cursorImg = QBitmap::fromData({32, 32}, vsplit_bits);
    static QBitmap mask = QBitmap::fromData({32, 32}, vsplitm_bits);
    return QCursor(scaledBitmap(cursorImg, ratio), scaledBitmap(mask, ratio),
                   15 * ratio, 15 * ratio);
}

class MiniSplitterHandle : public QSplitterHandle
{
public:
    MiniSplitterHandle(Qt::Orientation orientation, QSplitter *parent, bool lightColored = false)
            : QSplitterHandle(orientation, parent),
              m_lightColored(lightColored)
    {
        setMask(QRegion(contentsRect()));
        setAttribute(Qt::WA_MouseNoMask, true);
    }
protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_lightColored;
};

} // namespace Internal
} // namespace Core

using namespace Core;
using namespace Core::Internal;

bool MiniSplitterHandle::event(QEvent *event)
{
    if (generalSettings().provideSplitterCursors()) {
        if (event->type() == QEvent::HoverEnter) {
            const qreal ratio = screen()->devicePixelRatio();
            setCursor(orientation() == Qt::Horizontal ? hsplitCursor(ratio) : vsplitCursor(ratio));
        } else if (event->type() == QEvent::HoverLeave) {
            unsetCursor();
        }
    }
    return QSplitterHandle::event(event);
}

void MiniSplitterHandle::resizeEvent(QResizeEvent *event)
{
    if (orientation() == Qt::Horizontal)
        setContentsMargins(2, 0, 2, 0);
    else
        setContentsMargins(0, 2, 0, 2);
    setMask(QRegion(contentsRect()));
    QSplitterHandle::resizeEvent(event);
}

void MiniSplitterHandle::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    const QColor color = Utils::creatorTheme()->color(
                m_lightColored ? Utils::Theme::FancyToolBarSeparatorColor
                               : Utils::Theme::SplitterColor);
    painter.fillRect(event->rect(), color);
}

/*!
    \class Core::MiniSplitter
    \inheaderfile coreplugin/minisplitter.h
    \inmodule QtCreator

    \brief The MiniSplitter class is a simple helper-class to obtain
    \macos style 1-pixel wide splitters.
*/

/*!
    \enum Core::MiniSplitter::SplitterStyle
    This enum value holds the splitter style.

    \value Dark  Dark style.
    \value Light Light style.
*/

QSplitterHandle *MiniSplitter::createHandle()
{
    return new MiniSplitterHandle(orientation(), this, m_style == Light);
}

MiniSplitter::MiniSplitter(QWidget *parent, SplitterStyle style)
    : QSplitter(parent),
      m_style(style)
{
    setHandleWidth(1);
    setChildrenCollapsible(false);
    setProperty(Utils::StyleHelper::C_MINI_SPLITTER, true);
}

MiniSplitter::MiniSplitter(Qt::Orientation orientation, QWidget *parent, SplitterStyle style)
    : QSplitter(orientation, parent),
      m_style(style)
{
    setHandleWidth(1);
    setChildrenCollapsible(false);
    setProperty(Utils::StyleHelper::C_MINI_SPLITTER, true);
}

/*!
    \class Core::NonResizingSplitter
    \inheaderfile coreplugin/minisplitter.h
    \inmodule QtCreator

    \brief The NonResizingSplitter class is a MiniSplitter that keeps its
    first widget's size fixed when it is resized.
*/

/*!
    Constructs a non-resizing splitter with \a parent and \a style.

    The default style is MiniSplitter::Light.
*/
NonResizingSplitter::NonResizingSplitter(QWidget *parent, SplitterStyle style)
    : MiniSplitter(parent, style)
{
}

/*!
    \internal
*/
void NonResizingSplitter::resizeEvent(QResizeEvent *ev)
{
    // bypass QSplitter magic
    int leftSplitWidth = qMin(sizes().at(0), ev->size().width());
    int rightSplitWidth = qMax(0, ev->size().width() - leftSplitWidth);
    setSizes(QList<int>() << leftSplitWidth << rightSplitWidth);
    QWidget::resizeEvent(ev);
}
