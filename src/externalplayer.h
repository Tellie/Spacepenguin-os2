#pragma once

#include <QUrl>

namespace spacepenguin::ExternalPlayer {

/** True for a video page or link this can hand to yt-dlp (YouTube for now). */
bool canPlay(const QUrl &url);

/**
 * Plays @p url outside the browser: yt-dlp fetches the stream and pipes it into
 * a player. The command comes from the setting externalPlayer/command (or the
 * SPACEPENGUIN_PLAYER environment variable); %u in it is replaced by the URL.
 */
bool play(const QUrl &url);

} // namespace spacepenguin::ExternalPlayer
