#pragma once

// Compatibility layer so SpacePenguin also builds against older QtWebEngine
// releases (for example the OS/2 port, QtWebEngine 6.2.6), which lack
//   - QWebEnginePermission and QWebEnginePage::permissionRequested   (Qt 6.8)
//   - QWebEngineProfile::list/queryPermission(s)                     (Qt 6.8)
//   - QWebEngineProfile::setPersistentPermissionsPolicy               (Qt 6.8)
//   - QWebEngineUrlRequestJob::requestBody                            (Qt 6.7)
//   - QWebEngineUrlScheme::FetchApiAllowed                            (Qt 6.6)
//
// Call sites use the spcompat:: functions; with a new QtWebEngine they simply
// forward to the real API, so nothing changes there.
//
// On old QtWebEngine the permission answers are kept by this file: in memory,
// and for normal (not private) profiles also in
// <persistentStoragePath>/sitepermissions.ini, so they survive a restart.

#include <QIODevice>
#include <QList>
#include <QObject>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

#if __has_include(<QWebEnginePermission>)
#  include <QWebEnginePermission>
#  define SPACEPENGUIN_HAVE_QWEBENGINEPERMISSION 1
#else
#  define SPACEPENGUIN_HAVE_QWEBENGINEPERMISSION 0
#endif

// QTWEBENGINECORE_VERSION cannot be trusted here: a QtWebEngine built against a
// newer qtbase (the OS/2 port: 6.2.6 code against qtbase 6.8.3) reports the
// qtbase version. QWebEnginePermission (Qt 6.8) is the reliable sign of the
// new API; requestBody (6.7) and FetchApiAllowed (6.6) come with it.
#define SPACEPENGUIN_NEW_WEBENGINE_API SPACEPENGUIN_HAVE_QWEBENGINEPERMISSION

#if !SPACEPENGUIN_HAVE_QWEBENGINEPERMISSION

#include <QHash>
#include <QMap>
#include <QPair>
#include <QPointer>
#include <QSettings>
#include <QStringList>

class QWebEnginePermission;

namespace spcompat::detail {

// What the user answered, per profile: (origin, type) -> state.
struct ProfileStore
{
    bool loaded = false;
    bool onDisk = false;
    QMap<QPair<QString, int>, int> answers;
};

inline QHash<const QWebEngineProfile *, ProfileStore> &stores()
{
    static QHash<const QWebEngineProfile *, ProfileStore> s;
    return s;
}

inline QString normalizedOrigin(const QUrl &url)
{
    return url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment
                        | QUrl::RemoveUserInfo | QUrl::StripTrailingSlash)
        .toString();
}

inline QString iniPath(const QWebEngineProfile *profile)
{
    if (!profile || profile->isOffTheRecord() || profile->persistentStoragePath().isEmpty())
        return QString();
    return profile->persistentStoragePath() + QStringLiteral("/sitepermissions.ini");
}

inline ProfileStore &store(const QWebEngineProfile *profile)
{
    ProfileStore &s = stores()[profile];
    if (!s.loaded) {
        s.loaded = true;
        const QString path = iniPath(profile);
        if (!path.isEmpty()) {
            QSettings ini(path, QSettings::IniFormat);
            const int n = ini.beginReadArray(QStringLiteral("permissions"));
            for (int i = 0; i < n; ++i) {
                ini.setArrayIndex(i);
                s.answers.insert({ini.value(QStringLiteral("origin")).toString(),
                                  ini.value(QStringLiteral("type")).toInt()},
                                 ini.value(QStringLiteral("state")).toInt());
            }
            ini.endArray();
            if (n > 0)
                s.onDisk = true;
        }
    }
    return s;
}

inline void save(const QWebEngineProfile *profile)
{
    ProfileStore &s = store(profile);
    const QString path = iniPath(profile);
    if (!s.onDisk || path.isEmpty())
        return;
    QSettings ini(path, QSettings::IniFormat);
    ini.remove(QStringLiteral("permissions"));
    ini.beginWriteArray(QStringLiteral("permissions"), int(s.answers.size()));
    int i = 0;
    for (auto it = s.answers.cbegin(); it != s.answers.cend(); ++it, ++i) {
        ini.setArrayIndex(i);
        ini.setValue(QStringLiteral("origin"), it.key().first);
        ini.setValue(QStringLiteral("type"), it.key().second);
        ini.setValue(QStringLiteral("state"), it.value());
    }
    ini.endArray();
}

} // namespace spcompat::detail

/** Stand-in for Qt 6.8's QWebEnginePermission, built on featurePermissionRequested. */
class QWebEnginePermission
{
public:
    enum class PermissionType {
        Unsupported,
        MediaAudioCapture,
        MediaVideoCapture,
        MediaAudioVideoCapture,
        DesktopVideoCapture,
        DesktopAudioVideoCapture,
        MouseLock,
        Notifications,
        Geolocation,
        ClipboardReadWrite,
        LocalFontsAccess,
    };
    enum class State { Invalid, Ask, Granted, Denied };

    QWebEnginePermission() = default;
    QWebEnginePermission(QWebEngineProfile *profile, QWebEnginePage *page, const QUrl &requestOrigin,
                         PermissionType type)
        : m_profile(profile)
        , m_page(page)
        , m_requestOrigin(requestOrigin)
        , m_origin(QUrl(spcompat::detail::normalizedOrigin(requestOrigin)))
        , m_type(type)
    {
    }

    QUrl origin() const { return m_origin; }
    PermissionType permissionType() const { return m_type; }
    bool isValid() const { return m_profile && m_type != PermissionType::Unsupported; }

    State state() const
    {
        if (!isValid())
            return State::Invalid;
        const auto &answers = spcompat::detail::store(m_profile).answers;
        return State(answers.value({m_origin.toString(), int(m_type)}, int(State::Ask)));
    }

    void grant() const { answer(State::Granted); }
    void deny() const { answer(State::Denied); }
    void reset() const
    {
        if (!isValid())
            return;
        spcompat::detail::store(m_profile).answers.remove({m_origin.toString(), int(m_type)});
        spcompat::detail::save(m_profile);
    }

    static bool isPersistent(PermissionType type)
    {
        return type != PermissionType::MouseLock && type != PermissionType::DesktopVideoCapture
            && type != PermissionType::DesktopAudioVideoCapture;
    }

    // Mapping to and from the old QWebEnginePage::Feature.
    static PermissionType fromFeature(QWebEnginePage::Feature feature)
    {
        switch (feature) {
        case QWebEnginePage::Notifications: return PermissionType::Notifications;
        case QWebEnginePage::Geolocation: return PermissionType::Geolocation;
        case QWebEnginePage::MediaAudioCapture: return PermissionType::MediaAudioCapture;
        case QWebEnginePage::MediaVideoCapture: return PermissionType::MediaVideoCapture;
        case QWebEnginePage::MediaAudioVideoCapture: return PermissionType::MediaAudioVideoCapture;
        case QWebEnginePage::MouseLock: return PermissionType::MouseLock;
        case QWebEnginePage::DesktopVideoCapture: return PermissionType::DesktopVideoCapture;
        case QWebEnginePage::DesktopAudioVideoCapture: return PermissionType::DesktopAudioVideoCapture;
        }
        return PermissionType::Unsupported;
    }
    static bool toFeature(PermissionType type, QWebEnginePage::Feature *feature)
    {
        switch (type) {
        case PermissionType::Notifications: *feature = QWebEnginePage::Notifications; return true;
        case PermissionType::Geolocation: *feature = QWebEnginePage::Geolocation; return true;
        case PermissionType::MediaAudioCapture: *feature = QWebEnginePage::MediaAudioCapture; return true;
        case PermissionType::MediaVideoCapture: *feature = QWebEnginePage::MediaVideoCapture; return true;
        case PermissionType::MediaAudioVideoCapture: *feature = QWebEnginePage::MediaAudioVideoCapture; return true;
        case PermissionType::MouseLock: *feature = QWebEnginePage::MouseLock; return true;
        case PermissionType::DesktopVideoCapture: *feature = QWebEnginePage::DesktopVideoCapture; return true;
        case PermissionType::DesktopAudioVideoCapture: *feature = QWebEnginePage::DesktopAudioVideoCapture; return true;
        default: return false;
        }
    }

private:
    void answer(State state) const
    {
        if (!isValid())
            return;
        // Tell the page that is waiting (if any).
        QWebEnginePage::Feature feature;
        if (m_page && toFeature(m_type, &feature)) {
            m_page->setFeaturePermission(m_requestOrigin, feature,
                                         state == State::Granted
                                             ? QWebEnginePage::PermissionGrantedByUser
                                             : QWebEnginePage::PermissionDeniedByUser);
        }
        // Remember it, like Qt 6.8 does for persistent permission types.
        if (isPersistent(m_type)) {
            spcompat::detail::store(m_profile).answers.insert({m_origin.toString(), int(m_type)},
                                                              int(state));
            spcompat::detail::save(m_profile);
        }
    }

    QPointer<QWebEngineProfile> m_profile;
    QPointer<QWebEnginePage> m_page;
    QUrl m_requestOrigin;
    QUrl m_origin;
    PermissionType m_type = PermissionType::Unsupported;
};

#endif // !SPACEPENGUIN_HAVE_QWEBENGINEPERMISSION

namespace spcompat {

/** QWebEngineProfile::listAllPermissions() */
inline QList<QWebEnginePermission> listAllPermissions(QWebEngineProfile *profile)
{
#if SPACEPENGUIN_HAVE_QWEBENGINEPERMISSION
    return profile->listAllPermissions();
#else
    QList<QWebEnginePermission> list;
    const auto &answers = detail::store(profile).answers;
    for (auto it = answers.cbegin(); it != answers.cend(); ++it) {
        list.append(QWebEnginePermission(profile, nullptr, QUrl(it.key().first),
                                         QWebEnginePermission::PermissionType(it.key().second)));
    }
    return list;
#endif
}

/** QWebEngineProfile::listPermissionsForOrigin() */
inline QList<QWebEnginePermission> listPermissionsForOrigin(QWebEngineProfile *profile, const QUrl &origin)
{
#if SPACEPENGUIN_HAVE_QWEBENGINEPERMISSION
    return profile->listPermissionsForOrigin(origin);
#else
    const QString wanted = detail::normalizedOrigin(origin);
    QList<QWebEnginePermission> list;
    for (const QWebEnginePermission &permission : listAllPermissions(profile)) {
        if (permission.origin().toString() == wanted)
            list.append(permission);
    }
    return list;
#endif
}

/** QWebEngineProfile::queryPermission() */
inline QWebEnginePermission queryPermission(QWebEngineProfile *profile, const QUrl &origin,
                                            QWebEnginePermission::PermissionType type)
{
#if SPACEPENGUIN_HAVE_QWEBENGINEPERMISSION
    return profile->queryPermission(origin, type);
#else
    return QWebEnginePermission(profile, nullptr, origin, type);
#endif
}

/** QWebEngineProfile::setPersistentPermissionsPolicy(StoreOnDisk / StoreInMemory) */
inline void setPermissionsStoredOnDisk(QWebEngineProfile *profile, bool onDisk)
{
#if SPACEPENGUIN_HAVE_QWEBENGINEPERMISSION
    profile->setPersistentPermissionsPolicy(onDisk ? QWebEngineProfile::PersistentPermissionsPolicy::StoreOnDisk
                                                   : QWebEngineProfile::PersistentPermissionsPolicy::StoreInMemory);
#else
    detail::store(profile).onDisk = onDisk && !detail::iniPath(profile).isEmpty();
#endif
}

/**
 * connect(page, &QWebEnginePage::permissionRequested, context, handler)
 * The handler gets a QWebEnginePermission; on old QtWebEngine an earlier
 * answer is applied without asking again.
 */
template<typename Handler>
inline QMetaObject::Connection onPermissionRequested(QWebEnginePage *page, const QObject *context,
                                                     Handler handler)
{
#if SPACEPENGUIN_HAVE_QWEBENGINEPERMISSION
    return QObject::connect(page, &QWebEnginePage::permissionRequested, context, std::move(handler));
#else
    return QObject::connect(
        page, &QWebEnginePage::featurePermissionRequested, context,
        [page, handler = std::move(handler)](const QUrl &origin, QWebEnginePage::Feature feature) mutable {
            QWebEnginePermission permission(page->profile(), page, origin,
                                            QWebEnginePermission::fromFeature(feature));
            switch (permission.state()) {
            case QWebEnginePermission::State::Granted:
                page->setFeaturePermission(origin, feature, QWebEnginePage::PermissionGrantedByUser);
                return;
            case QWebEnginePermission::State::Denied:
                page->setFeaturePermission(origin, feature, QWebEnginePage::PermissionDeniedByUser);
                return;
            default:
                handler(permission);
            }
        });
#endif
}

/** QWebEngineUrlRequestJob::requestBody(): nullptr when this QtWebEngine has none. */
inline QIODevice *requestBody(QWebEngineUrlRequestJob *job)
{
#if SPACEPENGUIN_NEW_WEBENGINE_API
    return job->requestBody();
#else
    Q_UNUSED(job);
    return nullptr;
#endif
}

/** The scheme flags that let pages use fetch() on a custom scheme. */
inline QWebEngineUrlScheme::Flags fetchApiFlags()
{
#if SPACEPENGUIN_NEW_WEBENGINE_API
    return QWebEngineUrlScheme::FetchApiAllowed;
#else
    // Not in this QtWebEngine; CORS is the closest it has.
    return QWebEngineUrlScheme::CorsEnabled;
#endif
}

} // namespace spcompat
