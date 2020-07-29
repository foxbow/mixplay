#!/bin/bash
if [ -z "$(which cvlc)" ]; then
	echo "VLC is not installed!"
	exit
fi

MONITOR=$(pactl list | grep "N.*:.*\.monitor$" | awk '{print $NF}')

if [ $(echo "${MONITOR}" | wc -w) != 1 ]; then
	echo "Could not identify audio monitor!"
	exit
fi

IP=$(ip route get 1.1.1.1 | grep -oP 'src \K\S+')
PORT=2348
NAME=mpstream.mp3
CHAIN=\#transcode\{acodec=mp3,ab=128,channels=2\}:standard\{access=http,dst=0.0.0.0:${PORT}/${NAME}\}

echo "Starting stream on:"
echo "http://$(hostname):${PORT}/${NAME}"
echo "http://${IP}:${PORT}/${NAME}"

nohup cvlc pulse://${MONITOR} --sout ${CHAIN} > /dev/null 2> /dev/null &
