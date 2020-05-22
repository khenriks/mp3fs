# Docker Instructions

There is now a [Docker
image](https://hub.docker.com/repository/docker/khenriks/mp3fs) available for
mp3fs. Most users will probably prefer to use the command normally, but it's
available for those who want it.

## docker run

To use this with the `docker run` command, run:

``` console
$ docker run --device /dev/fuse \
  --cap-add SYS_ADMIN \
  --security-opt apparmor:unconfined \
  -v /home/khenriks/Music:/music:ro \
  -v /tmp/mp3:/mnt:shared \
  khenriks/mp3fs \
  -b 256
```

There's a lot here, so let's unpack the command.

  - `docker run` is how you run a docker container.
  - `--device /dev/fuse` is needed to allow FUSE access.
  - `--cap-add SYS_ADMIN` is necessary to mount filesystems inside Docker
    containers. This might not be needed in the future for FUSE (see
    [docker/for-linux\#321](https://github.com/docker/for-linux/issues/321)),
    but for now it's required.
  - `--security-opt apparmor:unconfined` is also needed to mount filesystems
    inside Docker.
  - `-v /home/khenriks/Music:/music:ro` mounts the host directory
    `/home/khenriks/Music` read-only in the container. This will be the input
    for mp3fs. The host directory can be replaced with anything, but `/music`
    is required because that's where the container expects it.
  - `-v /tmp/mp3:/mnt:shared` mounts the directory `/tmp/mp3` inside the
    container. This will be the output from mp3fs. Again, the host directory
    can be anything, but `/mnt` is required by the container. `shared` makes
    the mp3fs mount inside the container show up in the host directory outside.
  - `khenriks/mp3fs` is the name of the Docker image.
  - `-b 256` is the normal mp3fs flag for bitrate. You can use any mp3fs flags
    here, after the image name.

## Docker Compose

There's also a `docker-compose.yml` file available, if that's your jam. To use
it, run:

``` console
$ MUSICDIR=/home/khenriks/Music MP3DIR=/tmp/mp3 MP3FS_FLAGS="-b 256" \
  docker-compose up
```

This will do the same thing as the previous command, but is hopefully simpler
to use. `MUSICDIR`, `MP3DIR`, and `MP3FS_FLAGS` can be customized as desired.
