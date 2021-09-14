#include "desktopbackgroundwindow.h"
#include "desktop-background-manager.h"
#include "peony-desktop-application.h"
#include "desktop-icon-view.h"
#include "desktop-menu.h"
#include <QScreen>
#include <QPainter>
#include <QVariantAnimation>
#include <QTimeLine>
#include <KWindowSystem>

static int desktop_window_id = 0;
static QTimeLine *gTimeLine = nullptr;

DesktopBackgroundWindow::DesktopBackgroundWindow(QScreen *screen, QWidget *parent) : QMainWindow(parent)
{
    if (!gTimeLine) {
        gTimeLine = new QTimeLine(100);
    }
    connect(gTimeLine, &QTimeLine::finished, this, &DesktopBackgroundWindow::updateWindowGeometry);
    setAttribute(Qt::WA_X11NetWmWindowTypeDesktop);
    setAttribute(Qt::WA_TranslucentBackground);

    setContextMenuPolicy(Qt::CustomContextMenu);

    m_screen = screen;
    m_id = desktop_window_id;
    desktop_window_id++;
    move(screen->geometry().topLeft());
    setFixedSize(screen->geometry().size());
    setContentsMargins(0, 0, 0, 0);
    connect(screen, &QScreen::geometryChanged, this, QOverload<const QRect&>::of(&DesktopBackgroundWindow::setWindowGeometry));

    auto manager = DesktopBackgroundManager::globalInstance();
    connect(manager, &DesktopBackgroundManager::screensUpdated, this, QOverload<>::of(&DesktopBackgroundWindow::update));

    connect(this, &QWidget::customContextMenuRequested, this, [=](const QPoint &pos){
        qInfo()<<pos;
        auto index = PeonyDesktopApplication::getIconView()->indexAt(pos);
        if (!index.isValid()) {
            PeonyDesktopApplication::getIconView()->clearSelection();
        } else {
            if (!PeonyDesktopApplication::getIconView()->selectionModel()->selection().indexes().contains(index)) {
                PeonyDesktopApplication::getIconView()->clearSelection();
                PeonyDesktopApplication::getIconView()->selectionModel()->select(index, QItemSelectionModel::Select);
            }
        }

        QTimer::singleShot(1, this, [=]() {
            DesktopMenu menu(PeonyDesktopApplication::getIconView());
            if (PeonyDesktopApplication::getIconView()->getSelections().isEmpty()) {
                auto action = menu.addAction(QObject::tr("set background"));
                connect(action, &QAction::triggered, [=]() {
                    //go to control center set background
                    PeonyDesktopApplication::gotoSetBackground();
                });
            }

            for (auto screen : qApp->screens()) {
                if (screen->geometry().contains(pos));
                //menu.windowHandle()->setScreen(screen);
            }
            menu.exec(QCursor::pos());
            auto urisToEdit = menu.urisToEdit();
            if (urisToEdit.count() == 1) {
                QTimer::singleShot(
                            100, this, [=]() {
                    PeonyDesktopApplication::getIconView()->editUri(urisToEdit.first());
                });
            }
        });
    });
}

void DesktopBackgroundWindow::paintEvent(QPaintEvent *event)
{
    auto manager = DesktopBackgroundManager::globalInstance();
    if (!manager->getPaintBackground())
        return;

    QPainter p(this);
    if (manager->getUsePureColor()) {
        p.fillRect(this->rect(), manager->getColor());
    } else {
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        auto animation = manager->getAnimation();
        if (animation->state() == QVariantAnimation::Running) {
            auto opacity = animation->currentValue().toReal();
            if (manager->getBackgroundOption() == "centered") {
                //居中
                p.drawPixmap((m_screen->size().width() - manager->getBackPixmap().rect().width()) / 2,
                             (m_screen->size().height() - manager->getBackPixmap().rect().height()) / 2,
                             manager->getBackPixmap());
                p.setOpacity(opacity);
                p.drawPixmap((m_screen->size().width() - manager->getFrontPixmap().rect().width()) / 2,
                             (m_screen->size().height() - manager->getFrontPixmap().rect().height()) / 2,
                             manager->getFrontPixmap());
            } else if (manager->getBackgroundOption() == "stretched") {
                //拉伸
                p.drawPixmap(this->rect(), manager->getBackPixmap(), manager->getBackPixmap().rect());
                p.setOpacity(opacity);
                p.drawPixmap(this->rect(), manager->getFrontPixmap(), manager->getFrontPixmap().rect());
            } else if (manager->getBackgroundOption() == "scaled") {
                //填充
                p.drawPixmap(this->rect(), manager->getBackPixmap());
                p.setOpacity(opacity);
                p.drawPixmap(this->rect(), manager->getFrontPixmap());
            } else if (manager->getBackgroundOption() == "wallpaper") {
                //平铺
                int drawedWidth = 0;
                int drawedHeight = 0;
                while (1) {
                    drawedWidth = 0;
                    while (1) {
                        p.drawPixmap(drawedWidth, drawedHeight, manager->getBackPixmap());
                        drawedWidth += manager->getBackPixmap().width();
                        if (drawedWidth >= m_screen->size().width()) {
                            break;
                        }
                    }
                    drawedHeight += manager->getBackPixmap().height();
                    if (drawedHeight >= m_screen->size().height()) {
                        break;
                    }
                }
                p.setOpacity(opacity);
                drawedWidth = 0;
                drawedHeight = 0;
                while (1) {
                    drawedWidth = 0;
                    while (1) {
                        p.drawPixmap(drawedWidth, drawedHeight, manager->getFrontPixmap());
                        drawedWidth += manager->getFrontPixmap().width();
                        if (drawedWidth >= m_screen->size().width()) {
                            break;
                        }
                    }
                    drawedHeight += manager->getFrontPixmap().height();
                    if (drawedHeight >= m_screen->size().height()) {
                        break;
                    }
                }
            } else {
                p.drawPixmap(rect().adjusted(0, 0, -1, -1), manager->getBackPixmap(), manager->getBackPixmap().rect());
                p.setOpacity(opacity);
                p.drawPixmap(rect().adjusted(0, 0, -1, -1), manager->getFrontPixmap(), manager->getFrontPixmap().rect());
            }

        } else {
            if (manager->getBackgroundOption() == "centered") {
                p.drawPixmap((m_screen->size().width() - manager->getFrontPixmap().rect().width()) / 2,
                             (m_screen->size().height() - manager->getFrontPixmap().rect().height()) / 2,
                             manager->getFrontPixmap());
            } else if (manager->getBackgroundOption() == "stretched") {
                p.drawPixmap(this->rect(), manager->getFrontPixmap());

            } else if (manager->getBackgroundOption() == "scaled") {
                p.drawPixmap(this->rect(), manager->getFrontPixmap());

            } else if (manager->getBackgroundOption() == "wallpaper") {
                int drawedWidth = 0;
                int drawedHeight = 0;
                while (1) {
                    drawedWidth = 0;
                    while (1) {
                        p.drawPixmap(drawedWidth, drawedHeight, manager->getFrontPixmap());
                        drawedWidth += manager->getFrontPixmap().width();
                        if (drawedWidth >= m_screen->size().width()) {
                            break;
                        }
                    }
                    drawedHeight += manager->getFrontPixmap().height();
                    if (drawedHeight >= m_screen->size().height()) {
                        break;
                    }
                }
            } else {
                p.drawPixmap(rect().adjusted(0, 0, -1, -1), manager->getBackPixmap(), manager->getBackPixmap().rect());
            }
        }
        p.restore();
    }
}

QScreen *DesktopBackgroundWindow::screen() const
{
    return m_screen;
}

void DesktopBackgroundWindow::setWindowGeometry(const QRect &geometry)
{
    qInfo()<<"bg window geometry changed"<<screen()->name()<<geometry<<screen()->geometry();
    if (gTimeLine->state() != QTimeLine::Running) {
        gTimeLine->start();
    } else {
        gTimeLine->setCurrentTime(0);
    }
}

void DesktopBackgroundWindow::updateWindowGeometry()
{
    auto geometry = m_screen->geometry();
    move(geometry.topLeft());
    setFixedSize(geometry.size());

    // raise primary window to make sure icon view is visible.
    if (centralWidget()) {
        KWindowSystem::raiseWindow(this->winId());
    }
}

int DesktopBackgroundWindow::id() const
{
    return m_id;
}