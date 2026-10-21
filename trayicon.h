#ifndef TRAYICON_H
#define TRAYICON_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>

class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(QObject *parent = nullptr);
    ~TrayIcon();
    QSystemTrayIcon* getTrayIcon();

signals:
    void signalShowHide();
    void signalExit();

private:
    void initMenu();

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_menu;
    QAction *m_actionShowHide;
    QAction *m_actionExit;
};

#endif