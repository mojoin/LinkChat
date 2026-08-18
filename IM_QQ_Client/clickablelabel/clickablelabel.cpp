#include "clickablelabel.h"
#include <QMouseEvent>
#include <QCursor>

ClickableLabel::ClickableLabel(QWidget *parent)
    : QLabel(parent)
{
    // 鼠标移上去变手型
    setCursor(Qt::PointingHandCursor);
    updateStyle();
}

ClickableLabel::ClickableLabel(const QString &text, QWidget *parent)
    : QLabel(text, parent)
{
    setCursor(Qt::PointingHandCursor);
    updateStyle();
}

void ClickableLabel::updateStyle()
{
    QString color;
    if (m_pressed) {
        color = "#cc0000";          // 按下：红
    } else if (m_hovered) {
        color = "#ff6600";          // 悬浮：橙
    } else {
        color = "#0066cc";          // 默认：蓝
    }

    setStyleSheet(QString(
        "color: %1;"
        "text-decoration: underline;"
    ).arg(color));
}

// 鼠标进入：变橙色
void ClickableLabel::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    updateStyle();
    QLabel::enterEvent(event);
}

// 鼠标离开：恢复蓝色
void ClickableLabel::leaveEvent(QEvent *event)
{
    m_hovered = false;
    m_pressed = false;
    updateStyle();
    QLabel::leaveEvent(event);
}

// 鼠标按下：变红色
void ClickableLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        updateStyle();
    }
    QLabel::mousePressEvent(event);
}

// 鼠标释放：触发 clicked 信号
void ClickableLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        updateStyle();
        emit clicked();   // ★ 发出点击信号
    }
    QLabel::mouseReleaseEvent(event);
}
