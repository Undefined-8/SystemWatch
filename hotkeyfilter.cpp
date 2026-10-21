#include "hotkeyfilter.h"
#include <QDebug>

HotkeyFilter::HotkeyFilter(QObject *parent)
    : QObject(parent)
{
}

bool HotkeyFilter::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::User + 1) {
        if (mainWindow) {
            QMetaObject::invokeMethod(mainWindow, "onToggleOverlay");
        }
        return true;
    }
    return QObject::eventFilter(obj, event);
}