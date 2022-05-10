#!/bin/bash

COVPATH=../cov-analysis-linux64-2021.12.1/bin/

# temporary set coverity path to the right installation

if [ ! -d "${COVPATH}" ]; then
  echo "No Coverity found at ${COVPATH}"
  exit
fi

PATH="${COVPATH}:${PATH}"
rm -f *-cov.tgz
make distclean
cov-build --dir cov-int make
VERSION=$(git describe --tags --abbrev=1 --dirty=-dev --always)
tar -czf ${VERSION}-cov.tgz cov-int
echo -e "\nUpload: ${VERSION}-cov.tgz\n"
