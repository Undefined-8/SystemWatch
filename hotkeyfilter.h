#ifndef HOTKEYFILTER_H
#define HOTKEYFILTER_H

#include <QObject>
#include <QEvent>
#include <QMetaObject>

class HotkeyFilter : public QObject
{
    Q_OBJECT
public:
    explicit HotkeyFilter(QObject *parent = nullptr);

    QObject *mainWindow = nullptr;

protected:
    virtual bool eventFilter(QObject *obj, QEvent *event);
};

#endif