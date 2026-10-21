#include "gamelibrary.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QSet>
#include <algorithm>

static QString readAcfValue(const QString &contents, const QString &key)
{
    const QString pattern = QStringLiteral("\"%1\"\\s+\"([^\"]*)\"")
        .arg(QRegularExpression::escape(key));
    const QRegularExpressionMatch match = QRegularExpression(pattern).match(contents);
    return match.hasMatch() ? match.captured(1) : QString();
}

static void addSteamLibraries(const QString &steamRoot, QSet<QString> *libraries)
{
    if (steamRoot.isEmpty()) return;
    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(steamRoot));
    if (!QDir(root).exists()) return;
    libraries->insert(root.toLower());

    QFile libraryFile(QDir(root).filePath("steamapps/libraryfolders.vdf"));
    if (!libraryFile.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QString contents = QString::fromUtf8(libraryFile.readAll());
    const QRegularExpression patterns[] = {
        QRegularExpression(QStringLiteral("\"path\"\\s+\"([^\"]+)\"")),
        QRegularExpression(QStringLiteral("^\\s*\"\\d+\"\\s+\"([^\"]+)\""), QRegularExpression::MultilineOption)
    };
    for (unsigned int i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        QRegularExpressionMatchIterator matches = patterns[i].globalMatch(contents);
        while (matches.hasNext()) {
            QString path = matches.next().captured(1);
            path.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
            path = QDir::cleanPath(QDir::fromNativeSeparators(path));
            if (QDir(path).exists()) libraries->insert(path.toLower());
        }
    }
}

GameLibraryScanner::GameLibraryScanner(QObject *parent)
    : QThread(parent)
{
}

QList<GameEntry> GameLibraryScanner::games() const
{
    return m_games;
}

void GameLibraryScanner::run()
{
    QSet<QString> steamRoots;
    QSettings steamRegistry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    addSteamLibraries(steamRegistry.value(QStringLiteral("SteamPath")).toString(), &steamRoots);
    addSteamLibraries(QString::fromLocal8Bit(qgetenv("PROGRAMFILES(X86)")) + QStringLiteral("/Steam"), &steamRoots);
    addSteamLibraries(QString::fromLocal8Bit(qgetenv("PROGRAMFILES")) + QStringLiteral("/Steam"), &steamRoots);
    addSteamLibraries(QString::fromLocal8Bit(qgetenv("LOCALAPPDATA")) + QStringLiteral("/Programs/Steam"), &steamRoots);
    addSteamLibraries(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                      .filePath(QStringLiteral("../Steam")), &steamRoots);

    QSet<QString> seen;
    for (const QString &steamRoot : steamRoots) {
        const QString appsPath = QDir(steamRoot).filePath(QStringLiteral("steamapps"));
        const QStringList manifests = QDir(appsPath).entryList(QStringList() << QStringLiteral("appmanifest_*.acf"), QDir::Files);
        for (const QString &manifestName : manifests) {
            QFile manifest(QDir(appsPath).filePath(manifestName));
            if (!manifest.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            const QString contents = QString::fromUtf8(manifest.readAll());
            const QString appId = readAcfValue(contents, QStringLiteral("appid"));
            const QString name = readAcfValue(contents, QStringLiteral("name"));
            if (appId.isEmpty() || name.isEmpty() || seen.contains(QStringLiteral("steam:") + appId)) continue;
            seen.insert(QStringLiteral("steam:") + appId);
            GameEntry entry;
            entry.name = name;
            entry.platform = QStringLiteral("Steam");
            entry.launchUrl = QStringLiteral("steam://rungameid/%1").arg(appId);
            m_games.append(entry);
        }
    }

    const QString programData = QString::fromLocal8Bit(qgetenv("PROGRAMDATA"));
    const QString epicManifests = QDir(programData).filePath(
        QStringLiteral("Epic/EpicGamesLauncher/Data/Manifests"));
    const QStringList epicFiles = QDir(epicManifests).entryList(QStringList() << QStringLiteral("*.item"), QDir::Files);
    for (const QString &fileName : epicFiles) {
        QFile file(QDir(epicManifests).filePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) continue;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) continue;
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("bIsIncomplete")).toBool()) continue;
        const QString appName = object.value(QStringLiteral("AppName")).toString();
        const QString displayName = object.value(QStringLiteral("DisplayName")).toString();
        if (appName.isEmpty() || displayName.isEmpty() || seen.contains(QStringLiteral("epic:") + appName)) continue;
        seen.insert(QStringLiteral("epic:") + appName);
        GameEntry entry;
        entry.name = displayName;
        entry.platform = QStringLiteral("Epic");
        entry.launchUrl = QStringLiteral("com.epicgames.launcher://apps/%1?action=launch&silent=true").arg(appName);
        m_games.append(entry);
    }

    std::sort(m_games.begin(), m_games.end(), [](const GameEntry &left, const GameEntry &right) {
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });
}
