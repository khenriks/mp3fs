PATH=$PWD/../src:$PATH
export LC_ALL=C

setup () {
	mkdir /tmp/mp3
	mp3fs -d "$PWD/flac" /tmp/mp3 2>$0-debug.log &
	sleep 0.1
}

finish () {
	hash fusermount 2>&- && fusermount -u /tmp/mp3 || umount /tmp/mp3
	rmdir /tmp/mp3
}
