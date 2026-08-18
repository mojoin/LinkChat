#ifndef CLICKABLELABEL_H
#define CLICKABLELABEL_H

#include <QLabel>
#include <QMouseEvent>
#include <QEnterEvent>

class ClickableLabel : public QLabel
{
    Q_OBJECT

public:
    explicit ClickableLabel(QWidget *parent = nullptr);
    explicit ClickableLabel(const QString &text, QWidget *parent = nullptr);

signals:
    // 自定义信号：被点击时发出
    void clicked();

protected:
    // 鼠标进入控件
    void enterEvent(QEnterEvent *event) override;
    // 鼠标离开控件
    void leaveEvent(QEvent *event) override;
    // 鼠标按下
    void mousePressEvent(QMouseEvent *event) override;
    // 鼠标释放
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateStyle();   // 刷新颜色

    bool m_hovered = false;   // 当前是否悬浮
    bool m_pressed = false;   // 当前是否按下
};

#endif // CLICKABLELABEL_H
