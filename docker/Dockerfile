# Local-build image — mirrors the linux job of .github/workflows/build.yml
# (that job runs inside debian:11-slim). Builds the 32-bit ReHLDS engine.
#
# Source is NOT copied in; it is bind-mounted at runtime by docker-compose,
# so editing code on the host needs no image rebuild.
FROM debian:11-slim

# 32-bit multilib toolchain — the engine compiles as i386 (-m32).
# Package list is kept identical to the CI "Install dependencies" step.
# bullseye is EOL: same apt sources as the CI "Install dependencies" step.
RUN rm -f /etc/apt/sources.list.d/*.sources /etc/apt/sources.list.d/*.list \
 && printf '%s\n' \
      'deb http://archive.debian.org/debian bullseye main' \
      'deb http://archive.debian.org/debian bullseye-updates main' \
      'deb http://snapshot.debian.org/archive/debian-security/20260830T000000Z bullseye-security main' \
      > /etc/apt/sources.list \
 && echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99no-check-valid-until \
 && dpkg --add-architecture i386 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      gcc-multilib g++-multilib \
      build-essential \
      libc6-dev libc6-dev-i386 \
      git cmake rsync \
      g++ gcc \
      python3 \
      libarchive-zip-perl \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
ENTRYPOINT ["bash", "docker/build.sh"]
