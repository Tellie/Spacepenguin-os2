#include "externalplayer.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace spacepenguin::ExternalPlayer {
namespace {

// yt-dlp writes the stream to stdout and the player reads it from stdin, so
// nothing has to be downloaded first. "b" is the best format that holds both
// picture and sound; a player cannot merge separate streams from a pipe.
const char *const kDefaultCommand = "yt-dlp -q -f \"b[height<=720]/b\" -o - \"%u\" | vlc -";

QString command()
{
    const QByteArray env = qgetenv("SPACEPENGUIN_PLAYER");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    QSettings settings;
    const QString key = QStringLiteral("externalPlayer/command");
    if (!settings.contains(key))
        settings.setValue(key, QString::fromLatin1(kDefaultCommand)); // so it can be edited
    return settings.value(key).toString();
}

} // namespace

bool canPlay(const QUrl &url)
{
    if (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))
        return false;
    const QString host = url.host().toLower();
    const QString path = url.path();
    if (host == QLatin1String("youtu.be"))
        return path.size() > 1;
    if (host == QLatin1String("youtube.com") || host.endsWith(QLatin1String(".youtube.com"))
        || host == QLatin1String("youtube-nocookie.com") || host.endsWith(QLatin1String(".youtube-nocookie.com"))) {
        return path == QLatin1String("/watch") || path.startsWith(QLatin1String("/shorts/"))
            || path.startsWith(QLatin1String("/embed/")) || path.startsWith(QLatin1String("/live/"));
    }
    return false;
}

bool play(const QUrl &url)
{
    if (!canPlay(url))
        return false;
    // Only characters that are safe inside double quotes on a command line.
    QString address = url.toString(QUrl::FullyEncoded);
    address.remove(QLatin1Char('"'));
    address.replace(QLatin1Char('%'), QLatin1String("%%")); // CMD batch file: a literal %
    QString line = command();
    line.replace(QLatin1String("%u"), address);

    // A pipe needs the command interpreter; a small batch file avoids having to
    // quote the whole line once more for it.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString script = QDir(dir).filePath(QStringLiteral("sp-player.cmd"));
    QFile file(script);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    file.write("@echo off\r\n");
    file.write(line.toLocal8Bit());
    file.write("\r\n");
    file.close();
#if defined(__OS2__) || defined(Q_OS_WIN)
    return QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QDir::toNativeSeparators(script)});
#else
    return QProcess::startDetached(QStringLiteral("/bin/sh"), {script});
#endif
}

} // namespace spacepenguin::ExternalPlayer
