#!/bin/sh
# ---------------------------------------------------------------------------
#  package-os2.sh - SpacePenguin verpakken voor andere OS/2-gebruikers
#
#  Maakt een map dist/SpacePenguin met:
#    - SpacePenguin.exe en een startscript (SpacePenguin.cmd)
#    - alle DLL's die het programma nodig heeft en die van H: komen
#      (Qt 6.8.3, QtWebEngine 6.2.6 en je eigen builds), ook indirect
#    - Qt-plugins, QtWebEngineProcess.exe, de WebEngine-resources en -locales
#    - LEESMIJ.txt met de RPM-pakketten die gebruikers zelf moeten installeren
#  en, als 7z of 7za gevonden wordt, SpacePenguin-<versie>-os2.7z.
#
#  DLL's van C: (RPM, LIBC, OS/2 zelf) gaan NIET mee: die zet het script
#  alleen in LEESMIJ.txt, met het RPM-pakket erbij als rpm dat weet.
#
#  Gebruik (vanuit de bronmap, na gcc1520-683.cmd en na sh qt683.sh):
#     sh package-os2.sh
# ---------------------------------------------------------------------------

# ---- Instellingen (pas aan indien nodig) ----------------------------------
QT_PREFIX="h:/usr/qt6-683"     # qtbase 6.8.3 + QtWebEngine 6.2.6
BUILD_DIR="1520-683"           # waar qt683.sh bouwt
DIST_DIR="dist"
APP_NAME="SpacePenguin"
# Andere mappen op H: met DLL's (gcc-runtime, eigen libs). Deze gaan voor
# LIBPATH, zodat bv. de gcc 15-runtime van H: wint van de RPM-versie op C:.
EXTRA_DIRS="h:/usr/lib h:/usr/bin"
# Plugin-mappen die mee moeten (bestaat er een niet, dan wordt hij overgeslagen)
PLUGIN_DIRS="platforms imageformats iconengines styles tls networkinformation platforminputcontexts"
# ---------------------------------------------------------------------------

# ---- Kleuren ----
ESC=`printf '\033'`
C_RESET="${ESC}[0m"
C_CYAN="${ESC}[1;36m"
C_GREEN="${ESC}[1;32m"
C_YELLOW="${ESC}[1;33m"
C_RED="${ESC}[1;31m"

say()  { echo ""; echo "${C_CYAN}=== $* ===${C_RESET}"; }
ok()   { echo "${C_GREEN}$*${C_RESET}"; }
warn() { echo "${C_YELLOW}LET OP: $*${C_RESET}"; }
die()  { echo ""; echo "${C_RED}*** FOUT: $* ***${C_RESET}"; exit 1; }

SRC_DIR=`pwd`
VERSION=`sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1`
[ -n "$VERSION" ] || VERSION="dev"
PKG="$DIST_DIR/$APP_NAME"
ARCHIVE="$APP_NAME-$VERSION-os2.7z"

# ---- 1. Controleren ----
say "Controleren"
EXE=""
for f in "$BUILD_DIR/SpacePenguin.exe" "$BUILD_DIR/spacepenguin.exe"; do
    [ -f "$f" ] && EXE="$f" && break
done
[ -n "$EXE" ] || die "Geen SpacePenguin.exe in $BUILD_DIR - eerst bouwen met: sh qt683.sh"
[ -d "$QT_PREFIX" ] || die "Qt niet gevonden in $QT_PREFIX"
command -v python3 >/dev/null 2>&1 || die "python3 niet gevonden (nodig om de DLL-afhankelijkheden te lezen)"
ok "Programma : $EXE"
ok "Qt        : $QT_PREFIX"
ok "Versie    : $VERSION"

# QtWebEngine-bestanden zoeken (Qt zet ze in libexec/ en resources/)
WE_PROCESS=""
for f in "$QT_PREFIX/libexec/QtWebEngineProcess.exe" "$QT_PREFIX/bin/QtWebEngineProcess.exe"; do
    [ -f "$f" ] && WE_PROCESS="$f" && break
done
[ -n "$WE_PROCESS" ] || die "QtWebEngineProcess.exe niet gevonden in $QT_PREFIX/libexec of /bin"
WE_RESOURCES="$QT_PREFIX/resources"
[ -d "$WE_RESOURCES" ] || die "Map met WebEngine-resources niet gevonden: $WE_RESOURCES"
WE_LOCALES="$QT_PREFIX/translations/qtwebengine_locales"
[ -d "$WE_LOCALES" ] || warn "Geen $WE_LOCALES - de WebEngine-vertalingen gaan niet mee"

# ---- 2. Pakketmap klaarzetten ----
say "Pakketmap $PKG klaarzetten"
rm -rf "$PKG"
mkdir -p "$PKG/plugins" "$PKG/libexec" "$PKG/resources" "$PKG/translations" \
    || die "Kan $PKG niet aanmaken"

cp "$EXE" "$PKG/$APP_NAME.exe" || die "Kopieren van $EXE mislukt"
cp "$WE_PROCESS" "$PKG/libexec/" || die "Kopieren van QtWebEngineProcess.exe mislukt"
cp "$WE_RESOURCES"/* "$PKG/resources/" || die "Kopieren van de resources mislukt"
if [ -d "$WE_LOCALES" ]; then
    mkdir -p "$PKG/translations/qtwebengine_locales"
    cp "$WE_LOCALES"/*.pak "$PKG/translations/qtwebengine_locales/"
fi
for d in $PLUGIN_DIRS; do
    if [ -d "$QT_PREFIX/plugins/$d" ]; then
        mkdir -p "$PKG/plugins/$d"
        cp "$QT_PREFIX/plugins/$d"/*.dll "$PKG/plugins/$d/" 2>/dev/null
        ok "plugins/$d"
    fi
done

# qt.conf: laat Qt de plugins, QtWebEngineProcess en resources in deze map zoeken
cat > "$PKG/qt.conf" <<'EOF'
[Paths]
Prefix=.
Plugins=plugins
LibraryExecutables=libexec
Data=.
Translations=translations
EOF
cat > "$PKG/libexec/qt.conf" <<'EOF'
[Paths]
Prefix=..
Plugins=plugins
LibraryExecutables=libexec
Data=.
Translations=translations
EOF

# ---- 3. DLL-afhankelijkheden van H: verzamelen ----
say "DLL-afhankelijkheden zoeken"

# Zoekpad: Qt, EXTRA_DIRS, BEGINLIBPATH, LIBPATH uit CONFIG.SYS, ENDLIBPATH.
# BEGIN-/ENDLIBPATH zijn geen gewone omgevingsvariabelen; CMD.EXE kent ze wel.
SEARCH="$QT_PREFIX/bin;$QT_PREFIX/lib"
for d in $EXTRA_DIRS; do SEARCH="$SEARCH;$d"; done
BLP=`cmd /c echo %BEGINLIBPATH% 2>/dev/null | tr -d '\r'`
ELP=`cmd /c echo %ENDLIBPATH% 2>/dev/null | tr -d '\r'`
case "$BLP" in *%*) BLP="" ;; esac
case "$ELP" in *%*) ELP="" ;; esac
SEARCH="$SEARCH;$BLP"
LIBPATH_SYS=`tr -d '\r' < c:/config.sys 2>/dev/null | sed -n 's/^[Ll][Ii][Bb][Pp][Aa][Tt][Hh]=//p' | head -1`
SEARCH="$SEARCH;$LIBPATH_SYS;$ELP"

DEPS_PY="$DIST_DIR/lxdeps.py"
cat > "$DEPS_PY" <<'PYEOF'
# Leest de import-moduletabel van OS/2 LX-bestanden (exe/dll) en zoekt
# recursief alle DLL's op. Uitvoer per regel:
#   H <pad>      DLL van H: (moet mee)
#   C <pad>      DLL van een andere schijf (RPM/systeem, gaat niet mee)
#   ? <naam>     niet gevonden (meestal OS/2-systeem-DLL zoals DOSCALLS)
import os, struct, sys

def imports(path):
    with open(path, 'rb') as f:
        data = f.read()
    off = 0
    if data[:2] == b'MZ':
        off = struct.unpack_from('<I', data, 0x3C)[0]
    if data[off:off + 2] != b'LX':
        return []
    table, count = struct.unpack_from('<II', data, off + 0x70)
    pos, names = off + table, []
    for _ in range(count):
        n = data[pos]
        names.append(data[pos + 1:pos + 1 + n].decode('ascii', 'replace'))
        pos += 1 + n
    return names

def main():
    search = [d for d in sys.argv[1].split(';') if d.strip() and d.strip() != '.']
    roots = sys.argv[2:]
    index = {}
    for d in search:
        try:
            for name in os.listdir(d):
                if name.lower().endswith('.dll'):
                    index.setdefault(name[:-4].upper(), os.path.join(d, name))
        except OSError:
            pass
    seen, todo, out = set(), list(roots), []
    while todo:
        path = todo.pop()
        for mod in imports(path):
            key = mod.upper()
            if key in seen:
                continue
            seen.add(key)
            found = index.get(key)
            if not found:
                out.append('? ' + mod)
            elif found[:2].lower() == 'h:':
                out.append('H ' + found)
                todo.append(found)
            else:
                out.append('C ' + found)
    print('\n'.join(sorted(out)))

main()
PYEOF

ROOTS="$PKG/$APP_NAME.exe $PKG/libexec/QtWebEngineProcess.exe"
for f in "$PKG"/plugins/*/*.dll; do
    [ -f "$f" ] && ROOTS="$ROOTS $f"
done
python3 -I "$DEPS_PY" "$SEARCH" $ROOTS > "$DIST_DIR/deps.txt" \
    || die "Het lezen van de afhankelijkheden is mislukt"

N_H=0
for dll in `sed -n 's/^H //p' "$DIST_DIR/deps.txt"`; do
    cp "$dll" "$PKG/" || die "Kopieren van $dll mislukt"
    N_H=`expr $N_H + 1`
done
ok "$N_H DLL's van H: meegenomen"
N_C=`grep -c '^C ' "$DIST_DIR/deps.txt"`
ok "$N_C DLL's van C: (RPM/systeem) - niet meegenomen"
# Niet gevonden: alleen de modules in de OS/2-kernel horen zo te zijn
MISSING=`sed -n 's/^? //p' "$DIST_DIR/deps.txt" | tr 'a-z' 'A-Z' | \
    grep -v -x -e DOSCALLS -e NLS -e MSG -e QUECALLS -e SESMGR -e VIOCALLS \
               -e KBDCALLS -e MOUCALLS -e MONCALLS`
if [ -n "$MISSING" ]; then
    warn "deze DLL's zijn niet gevonden - zet hun map in EXTRA_DIRS:"
    for m in $MISSING; do echo "${C_YELLOW}    $m.DLL${C_RESET}"; done
fi

# ---- 4. Startscript en LEESMIJ ----
say "Startscript en LEESMIJ.txt schrijven"
cat > "$PKG/$APP_NAME.cmd" <<'EOF'
/* SpacePenguin starten vanuit deze map */
'@echo off'
parse source . . self
dir = filespec('drive', self) || filespec('path', self)
dir = strip(dir, 'T', '\')
call directory dir
'set BEGINLIBPATH=' || dir || ';%BEGINLIBPATH%'
'set LIBPATHSTRICT=T'
'start "SpacePenguin" /n SpacePenguin.exe' arg(1)
EOF

{
    echo "SpacePenguin $VERSION voor OS/2 (ArcaOS)"
    echo "========================================"
    echo ""
    echo "Gebouwd met gcc 15.2.0, Qt 6.8.3 en QtWebEngine 6.2.6 (Chromium 94)."
    echo "Starten: SpacePenguin.cmd"
    echo ""
    echo "Let op: Chromium 94 is oud en mist veel beveiligingsfixes."
    echo "Gebruik deze browser niet voor internetbankieren."
    echo ""
    echo "Benodigde RPM-pakketten"
    echo "-----------------------"
    echo "Deze DLL's zitten niet in het pakket. Installeer ze met yum/rpm:"
    echo ""
    PKGS=""
    for dll in `sed -n 's/^C //p' "$DIST_DIR/deps.txt"`; do
        RPM=`rpm -qf "$dll" --qf '%{NAME}\n' 2>/dev/null | head -1`
        case "$RPM" in
            ""|*"not owned"*|*"niet"*) echo "  $dll" ;;
            *) echo "  $dll  ($RPM)"; PKGS="$PKGS $RPM" ;;
        esac
    done
    if [ -n "$PKGS" ]; then
        echo ""
        echo "In een keer installeren:"
        echo ""
        echo "  yum install`echo $PKGS | tr ' ' '\n' | sort -u | tr '\n' ' ' | sed 's/^/ /'`"
    fi
} > "$PKG/LEESMIJ.txt"
ok "LEESMIJ.txt geschreven"

# ---- 5. Archief ----
say "Archief maken"
SEVENZ=""
for z in 7z 7za; do
    command -v $z >/dev/null 2>&1 && SEVENZ=$z && break
done
if [ -n "$SEVENZ" ]; then
    rm -f "$DIST_DIR/$ARCHIVE"
    (cd "$DIST_DIR" && $SEVENZ a -mx=9 "$ARCHIVE" "$APP_NAME" >/dev/null) \
        || die "7z is mislukt"
    ok "Klaar: $DIST_DIR/$ARCHIVE"
else
    warn "geen 7z of 7za gevonden - de map $PKG staat klaar om zelf in te pakken"
fi

say "Klaar"
ok "Pakketmap : $PKG"
ok "Overzicht : $DIST_DIR/deps.txt  (H = mee, C = RPM/systeem, ? = OS/2-systeem)"
