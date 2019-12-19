#! /bin/bash
if [ "$1" == "" ]; then
	find . -type d -exec $0 \{\} \;
else
	DIR=$*

	TEST=$(find "${DIR}" -maxdepth 1 -name "*.mp3")

	if [ "${TEST}" != "" ]; then
		if [ ! -f "${DIR}/.gain" ]; then
			echo "Leveling ${DIR}"
			touch "${DIR}/.gain"
			mp3gain -q -k -r -T -p "${DIR}"/*mp3 2> /dev/null > /dev/null
		fi
	fi
fi
