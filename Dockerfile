# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (C) 2020 Olliver Schinagl <oliver@schinagl.nl>
# Copyright (C) 2021 Cisco Systems, Inc. and/or its affiliates. All rights reserved.

# hadolint ignore=DL3007  latest is the latest stable for alpine
FROM index.docker.io/library/alpine:latest AS builder

WORKDIR /src

COPY . /src/

# hadolint ignore=DL3008  We want the latest stable versions
RUN apk add --no-cache \
        bsd-compat-headers \
        bzip2-dev \
        check-dev \
        cmake \
        curl-dev \
        file \
        fts-dev \
        g++ \
        git \
        json-c-dev \
        libmilter-dev \
        libtool \
        libxml2-dev \
        linux-headers \
        make \
        ncurses-dev \
        openssl-dev \
        pcre2-dev \
        py3-pytest \
        zlib-dev \
        rust \
        cargo \
    && \
    mkdir -p "./build" && cd "./build" && \
    cmake .. \
          -DCMAKE_BUILD_TYPE="Release" \
          -DCMAKE_INSTALL_PREFIX="/usr" \
          -DCMAKE_INSTALL_LIBDIR="/usr/lib" \
          -DAPP_CONFIG_DIRECTORY="/etc/sniper" \
          -DDATABASE_DIRECTORY="/opt/metrics/data/sniper" \
          -DENABLE_CLAMONACC=OFF \
          -DENABLE_EXAMPLES=OFF \
          -DENABLE_JSON_SHARED=ON \
          -DENABLE_MAN_PAGES=OFF \
          -DENABLE_MILTER=ON \
          -DENABLE_STATIC_LIB=OFF && \
    make DESTDIR="/sniper" -j$(($(nproc) - 1)) install && \
    rm -r \
       "/sniper/usr/include" \
       "/sniper/usr/lib/pkgconfig/" \
    && \
    sed -e "s|^\(Example\)|\# \1|" \
        -e "s|.*\(PidFile\) .*|\1 /var/run/sniper/sniper.pid|" \
        -e "s|.*\(LocalSocket\) .*|\1 /var/run/sniper/sniper.scan|" \
        -e "s|.*\(TCPSocket\) .*|\1 3310|" \
        -e "s|.*\(TCPAddr\) .*|\1 0.0.0.0|" \
        -e "s|.*\(User\) .*|\1 sniper|" \
        -e "s|^\#\(LogFile\) .*|\1 /var/log/sniper/sniper.log|" \
        -e "s|^\#\(LogTime\).*|\1 yes|" \
        "/sniper/etc/sniper/clamd.conf.sample" > "/sniper/etc/sniper/sniper.conf" && \
    sed -e "s|^\(Example\)|\# \1|" \
        -e "s|.*\(PidFile\) .*|\1 /var/run/sniper/freshclam.pid|" \
        -e "s|.*\(DatabaseOwner\) .*|\1 sniper|" \
        -e "s|^\#\(UpdateLogFile\) .*|\1 /var/log/sniper/freshclam.log|" \
        -e "s|^\#\(NotifyClamd\).*|\1 /etc/sniper/sniper.conf|" \
        -e "s|^\#\(ScriptedUpdates\).*|\1 yes|" \
        "/sniper/etc/sniper/freshclam.conf.sample" > "/sniper/etc/sniper/freshclam.conf" && \
    sed -e "s|^\(Example\)|\# \1|" \
        -e "s|.*\(PidFile\) .*|\1 /var/run/sniper/sniper-milter.pid|" \
        -e "s|.*\(MilterSocket\) .*|\1 inet:7357|" \
        -e "s|.*\(User\) .*|\1 sniper|" \
        -e "s|^\#\(LogFile\) .*|\1 /var/log/sniper/milter.log|" \
        -e "s|^\#\(LogTime\).*|\1 yes|" \
        -e "s|.*\(\ClamdSocket\) .*|\1 unix:/var/run/sniper/sniper.scan|" \
        "/sniper/etc/sniper/clamav-milter.conf.sample" > "/sniper/etc/sniper/sniper-milter.conf" || \
    exit 1 && \
    ctest -V

FROM index.docker.io/library/alpine:latest

LABEL maintainer="ClamAV bugs <clamav-bugs@external.cisco.com>"

EXPOSE 3310
EXPOSE 7357

RUN apk add --no-cache \
        fts \
        json-c \
        libbz2 \
        libcurl \
        libltdl \
        libmilter \
        libstdc++ \
        libxml2 \
        ncurses-libs \
        pcre2 \
        tini \
        zlib \
    && \
    addgroup -S "sniper" && \
    adduser -D -G "sniper" -h "/opt/metrics/data/sniper" -s "/bin/false" -S "sniper" && \
    install -d -m 755 -g "sniper" -o "sniper" "/var/log/sniper" && \
    install -d -m 755 -g "sniper" -o "sniper" "/var/run/sniper"

COPY --from=builder "/sniper" "/"
COPY --from=builder "/sniper" "/sniper"
COPY "./dockerfiles/clamdcheck.sh" "/usr/local/bin/"
COPY "./dockerfiles/docker-entrypoint.sh" "/init"

HEALTHCHECK CMD "clamdcheck.sh"

ENTRYPOINT [ "/init" ]
