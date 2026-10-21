#ifndef GAMELIBRARY_H
#define GAMELIBRARY_H

#include <QList>
#include <QString>
#include <QThread>

struct GameEntry
{
    QString name;
    QString platform;
    QString launchUrl;
};

class GameLibraryScanner : public QThread
{
public:
    explicit GameLibraryScanner(QObject *parent = nullptr);
    QList<GameEntry> games() const;

protected:
    void run() override;

private:
    QList<GameEntry> m_games;
};

#endif
