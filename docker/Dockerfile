FROM debian:buster-slim AS base

FROM base AS build

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    build-essential \
    ca-certificates \
    curl \
    libflac++-dev \
    libfuse-dev \
    libid3tag0-dev \
    libmp3lame-dev \
    libvorbis-dev \
    pandoc \
    && rm -rf /var/lib/apt/lists/*

ADD . /mp3fs

RUN cd /mp3fs && ./autogen.sh && ./configure && make && make install

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
