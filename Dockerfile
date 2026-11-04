# syntax=docker/dockerfile:1
#
# OmniCon-mk-II — multi-stage build.
#
#   docker build -t omnicon .
#   docker run -it --rm \
#       -e ORACLE_APP_HOST=147.224.62.87 -e ORACLE_APP_PORT=13306 \
#       -e ORACLE_APP_USER=omnicon -e ORACLE_APP_PASSWORD=... \
#       -e ORACLE_APP_SERVICE=omnicon \
#       -v "$PWD/Roms:/app/Roms" -v "$PWD/saves:/app/saves" \
#       omnicon
#
# The REPL needs an interactive terminal (-it). The SDL game shell
# additionally needs X11/WSLg display forwarding (e.g. -e DISPLAY=$DISPLAY
# -v /tmp/.X11-unix:/tmp/.X11-unix).

# ───────────────────────────── build stage ─────────────────────────────
FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential git curl ca-certificates python3 unzip xz-utils \
        libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev \
        default-libmysqlclient-dev libmgba-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# third_party/SameBoy is a gitlink (no .gitmodules), so a fresh clone leaves
# it empty. Fetch the commit pinned by this repository when sources are absent.
ARG SAMEBOY_COMMIT=208ba4afabffab9edde416f2dbb8ae459e34adb8
RUN if [ ! -f third_party/SameBoy/Makefile ]; then \
        git clone https://github.com/LIJI32/SameBoy.git third_party/SameBoy \
        && git -C third_party/SameBoy checkout -q "$SAMEBOY_COMMIT"; \
    fi

# install_deps.sh installs RGBDS and cppp into ~/.local/bin (Ubuntu 24.04 has
# no rgbds package); the SameBoy boot ROM build must find them on PATH.
ENV PATH="/root/.local/bin:${PATH}"

# Bootstraps RGBDS + cppp, then builds the SameBoy boot ROMs and libsameboy.a.
RUN bash scripts/install_deps.sh --build

RUN make

# ──────────────────────────── runtime stage ────────────────────────────
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        libsdl2-2.0-0 libsdl2-ttf-2.0-0 libsdl2-image-2.0-0 \
        libmysqlclient21 libmgba0.10 \
        fonts-dejavu-core curl unzip ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -s /bin/bash omnicon

WORKDIR /app

COPY --from=build --chown=omnicon:omnicon /app/omnicon_repl ./omnicon_repl
COPY --from=build --chown=omnicon:omnicon /app/scripts ./scripts
COPY --from=build --chown=omnicon:omnicon /app/Controls.csv ./Controls.csv
COPY --from=build --chown=omnicon:omnicon /app/retroarch_controller.cfg ./retroarch_controller.cfg
COPY --from=build --chown=omnicon:omnicon /app/retroarch_flycast.cfg ./retroarch_flycast.cfg
# Game Boy boot ROMs and libretro cores are loaded from these paths at runtime.
COPY --from=build --chown=omnicon:omnicon /app/third_party/SameBoy/build/bin/BootROMs ./third_party/SameBoy/build/bin/BootROMs
COPY --from=build --chown=omnicon:omnicon /app/third_party/libretro ./third_party/libretro

# Runtime data directories; mount volumes here to persist ROMs and saves.
RUN mkdir -p Roms saves && chown omnicon:omnicon Roms saves

USER omnicon

# A container cannot install or run the GUI emulators; skip the startup
# download check. Override with -e OMNICON_SKIP_DEP_CHECK= if unwanted.
ENV OMNICON_SKIP_DEP_CHECK=1

CMD ["./omnicon_repl"]
