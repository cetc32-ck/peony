#ifndef DESKTOPBACKGROUNDWINDOW_H
#define DESKTOPBACKGROUNDWINDOW_H

#include <QMainWindow>

class DesktopBackgroundWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit DesktopBackgroundWindow(QScreen *screen, QWidget *parent = nullptr);

    int id() const;

    QScreen *screen() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_id = -1;
    QScreen *m_screen = nullptr;
};

#endif // DESKTOPBACKGROUNDWINDOW_H
