#!/bin/sh
#
# qt683.sh - SpacePenguin bouwen op OS/2 tegen Qt 6.8.3 (h:/usr/qt6-683)
#
# Gebruik:
#   sh qt683.sh            bijwerken (git pull) en verder bouwen in 1520-683
#   sh qt683.sh clean      buildmap eerst weggooien en alles opnieuw bouwen
#   JOBS=2 sh qt683.sh     aantal parallelle compile-jobs kiezen
#
# Het script legt GEEN lokale wijzigingen meer vast: de broncode komt van
# github.com/Tellie/Spacepenguin-os2 en wordt alleen opgehaald.

# --- CONFIGURATIE ---
WAV_FILE="c:/usr/bin/Ready.wav"   # geluid na afloop
QT_PATH="h:/usr/qt6-683"
INSTALL_BASE="h:/qt6/Spacepenguin"
BUILD_DIR="1520-683"
LOG_FILE="1520_683.log"           # in de bronmap; *.log wordt door git genegeerd
# --------------------

# Kleuren (printf zet \033 om in ESC). Heldere varianten: beter leesbaar op zwart.
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
MAGENTA='\033[1;35m'
RED='\033[1;31m'
GREEN='\033[1;32m'
NC='\033[0m'

SRC_DIR=$(pwd)
LOG="$SRC_DIR/$LOG_FILE"
START_TIME=$(date +%s)

printf "${CYAN}=======================================${NC}\n"
printf "${CYAN}  OS/2 Build & Git Sync                ${NC}\n"
printf "${CYAN}=======================================${NC}\n"
printf "Gestart: %s\n" "$(date +'%d-%m-%Y %H:%M:%S')" > "$LOG"

# 1. Broncode bijwerken (alleen ophalen, niets vastleggen)
if [ -d ".git" ]; then
    printf "${YELLOW}Bijwerken vanaf GitHub (origin/main)...${NC}\n"
    if git pull --ff-only origin main >> "$LOG" 2>&1; then
        printf "${GREEN}Broncode is bijgewerkt: %s${NC}\n" "$(git log --oneline -1)"
    else
        printf "${RED}git pull is niet gelukt (zie %s). We bouwen met de huidige broncode.${NC}\n" "$LOG_FILE"
        printf "${RED}Lokale wijzigingen? Kijk met: git status${NC}\n"
    fi
fi

# 2. Build voorbereiden
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 1)}

OS2_LDFLAGS="-Zhigh-mem -Zmap -Zomf -fstack-protector"
OS2_CFLAGS="-O2 -g -march=pentium4"
OS2_LIBS="-lcx -lpthread"

if [ "$1" = "clean" ] && [ -d "$BUILD_DIR" ]; then
    printf "${YELLOW}Buildmap %s weggooien (clean build)...${NC}\n" "$BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR" || exit 1

# 3. CMake
printf "\n${BLUE}Configuring CMake...${NC}\n"
{
    cmake -G "Unix Makefiles" \
        -DSPACEPENGUIN_BUILD_TESTS=OFF \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_PATH" \
        -DCMAKE_SYSTEM_NAME=OS2 \
        -DCMAKE_CXX_FLAGS="$OS2_CFLAGS" \
        -DCMAKE_EXE_LINKER_FLAGS="-L$QT_PATH/lib $OS2_LDFLAGS $OS2_LIBS" \
        -DCMAKE_SHARED_LINKER_FLAGS="-L$QT_PATH/lib $OS2_LDFLAGS $OS2_LIBS" \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_BASE" \
        .. 2>&1
    echo $? > .cmake_status
} | tee -a "$LOG"

if [ "$(cat .cmake_status)" != "0" ]; then
    printf "\n${RED}CMake is mislukt; zie %s.${NC}\n" "$LOG_FILE"
    FAILED=1
fi

# Projectnaam achterhalen
PROJECT_NAME=$(grep "CMAKE_PROJECT_NAME:STATIC=" CMakeCache.txt 2>/dev/null | cut -d'=' -f2)
[ -z "$PROJECT_NAME" ] && PROJECT_NAME="unknown_project"

# 4. Bouwen
if [ -z "$FAILED" ]; then
    printf "\n${BLUE}Building: ${YELLOW}%s${NC} (%s jobs)...\n" "$PROJECT_NAME" "$JOBS"
    {
        make -j"$JOBS" 2>&1
        echo $? > .make_status
    } | tee -a "$LOG"
    if [ "$(cat .make_status)" != "0" ]; then
        printf "\n${RED}Bouwen is mislukt; zie %s.${NC}\n" "$LOG_FILE"
        FAILED=1
    fi
fi

# 5. Installeren (alleen als alles gelukt is)
if [ -z "$FAILED" ]; then
    printf "\n${MAGENTA}Installeren naar: %s/%s${NC}\n" "$INSTALL_BASE" "$PROJECT_NAME"
    cmake --install . --prefix "$INSTALL_BASE/$PROJECT_NAME" 2>&1 | tee -a "$LOG"
fi

# 6. Tijd en geluid
END_TIME=$(date +%s)
TOTAL=$((END_TIME - START_TIME))
MINS=$((TOTAL / 60))
SECS=$((TOTAL % 60))

if [ -z "$FAILED" ]; then
    printf "\n${GREEN}Klaar! Duur: %s sec (%s min %s sec)${NC}\n" "$TOTAL" "$MINS" "$SECS"
else
    printf "\n${RED}Gestopt met fouten na %s min %s sec.${NC}\n" "$MINS" "$SECS"
fi

if [ -f "$WAV_FILE" ]; then
    playwave.exe "$WAV_FILE" > /dev/null 2>&1 &
else
    for i in 1 2 3; do
        printf '\a'
        sleep 1
    done
fi

[ -z "$FAILED" ]
