#! /bin/bash
if [ "$1" == "" ]; then
	find . -type d -exec $0 \{\} \;
else
	DIR=$*

	TEST=$(find "${DIR}" -maxdepth 1 -name "*.mp3")

	if [ "${TEST}" != "" ]; then
		echo "Leveling ${DIR}"
		mp3gain -q -T -p -s i "${DIR}"/*mp3 2> /dev/null > /dev/null
	fi
fi
