/****************************************************************************
** Meta object code from reading C++ file 'systemmonitor.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.9.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../systemmonitor.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'systemmonitor.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.9.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SystemMonitor_t {
    QByteArrayData data[15];
    char stringdata0[199];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SystemMonitor_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SystemMonitor_t qt_meta_stringdata_SystemMonitor = {
    {
QT_MOC_LITERAL(0, 0, 13), // "SystemMonitor"
QT_MOC_LITERAL(1, 14, 11), // "dataUpdated"
QT_MOC_LITERAL(2, 26, 0), // ""
QT_MOC_LITERAL(3, 27, 24), // "foregroundProcessChanged"
QT_MOC_LITERAL(4, 52, 10), // "executable"
QT_MOC_LITERAL(5, 63, 9), // "processId"
QT_MOC_LITERAL(6, 73, 21), // "recordingStateChanged"
QT_MOC_LITERAL(7, 95, 9), // "recording"
QT_MOC_LITERAL(8, 105, 4), // "path"
QT_MOC_LITERAL(9, 110, 7), // "message"
QT_MOC_LITERAL(10, 118, 16), // "initializeWorker"
QT_MOC_LITERAL(11, 135, 14), // "shutdownWorker"
QT_MOC_LITERAL(12, 150, 20), // "onUpdateTimerTimeout"
QT_MOC_LITERAL(13, 171, 19), // "setRecordingEnabled"
QT_MOC_LITERAL(14, 191, 7) // "enabled"

    },
    "SystemMonitor\0dataUpdated\0\0"
    "foregroundProcessChanged\0executable\0"
    "processId\0recordingStateChanged\0"
    "recording\0path\0message\0initializeWorker\0"
    "shutdownWorker\0onUpdateTimerTimeout\0"
    "setRecordingEnabled\0enabled"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SystemMonitor[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   49,    2, 0x06 /* Public */,
       3,    2,   50,    2, 0x06 /* Public */,
       6,    3,   55,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      10,    0,   62,    2, 0x08 /* Private */,
      11,    0,   63,    2, 0x08 /* Private */,
      12,    0,   64,    2, 0x08 /* Private */,
      13,    1,   65,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::UInt,    4,    5,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString, QMetaType::QString,    7,    8,    9,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   14,

       0        // eod
};

void SystemMonitor::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        SystemMonitor *_t = static_cast<SystemMonitor *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->dataUpdated(); break;
        case 1: _t->foregroundProcessChanged((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< uint(*)>(_a[2]))); break;
        case 2: _t->recordingStateChanged((*reinterpret_cast< bool(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 3: _t->initializeWorker(); break;
        case 4: _t->shutdownWorker(); break;
        case 5: _t->onUpdateTimerTimeout(); break;
        case 6: _t->setRecordingEnabled((*reinterpret_cast< bool(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            typedef void (SystemMonitor::*_t)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SystemMonitor::dataUpdated)) {
                *result = 0;
                return;
            }
        }
        {
            typedef void (SystemMonitor::*_t)(const QString & , uint );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SystemMonitor::foregroundProcessChanged)) {
                *result = 1;
                return;
            }
        }
        {
            typedef void (SystemMonitor::*_t)(bool , const QString & , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SystemMonitor::recordingStateChanged)) {
                *result = 2;
                return;
            }
        }
    }
}

const QMetaObject SystemMonitor::staticMetaObject = {
    { &QObject::staticMetaObject, qt_meta_stringdata_SystemMonitor.data,
      qt_meta_data_SystemMonitor,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *SystemMonitor::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SystemMonitor::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SystemMonitor.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int SystemMonitor::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void SystemMonitor::dataUpdated()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void SystemMonitor::foregroundProcessChanged(const QString & _t1, uint _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void SystemMonitor::recordingStateChanged(bool _t1, const QString & _t2, const QString & _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
