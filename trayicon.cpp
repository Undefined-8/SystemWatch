#include "trayicon.h"

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
{
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(QIcon(":/res/icon.png"));
    m_trayIcon->setToolTip("Game Monitor");

    initMenu();
    m_trayIcon->setContextMenu(m_menu);

    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick) {
                    emit signalShowHide();
                }
            });

    m_trayIcon->show();
}

TrayIcon::~TrayIcon()
{
}

QSystemTrayIcon* TrayIcon::getTrayIcon()
{
    return m_trayIcon;
}

void TrayIcon::initMenu()
{
    m_menu = new QMenu();

    m_actionShowHide = new QAction("Show/Hide", m_menu);
    m_actionShowHide->setShortcut(QKeySequence("Alt+F12"));
    connect(m_actionShowHide, &QAction::triggered, this, &TrayIcon::signalShowHide);

    QAction *separator = new QAction(m_menu);
    separator->setSeparator(true);

    m_actionExit = new QAction("Exit", m_menu);
    connect(m_actionExit, &QAction::triggered, this, &TrayIcon::signalExit);

    m_menu->addAction(m_actionShowHide);
    m_menu->addAction(separator);
    m_menu->addAction(m_actionExit);
}