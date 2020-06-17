FROM debian:buster-slim AS base

FROM base AS build

ARG VERSION=1.0
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    curl \
    libflac++-dev \
    libfuse-dev \
    libid3tag0-dev \
    libmp3lame-dev \
    libvorbis-dev \
    && rm -rf /var/lib/apt/lists/*

RUN curl -sL https://github.com/khenriks/mp3fs/releases/download/v${VERSION}/mp3fs-${VERSION}.tar.gz | \
    tar -xz && \
    cd mp3fs-${VERSION} && \
    ./configure && make && make install

FROM base

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    fuse \
    libflac++6v5 \
    libid3tag0 \
    libmp3lame0 \
    libvorbisfile3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /usr/local /usr/local

RUN mkdir /music

ENTRYPOINT ["mp3fs", "-d", "/music", "/mnt", "-oallow_other"]
