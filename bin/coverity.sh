#!/bin/bash

# temporary set coverity path to the right installation
PATH=../cov-analysis-linux64-2019.03/bin/:${PATH}

rm -f *-cov.tgz
make distclean
cov-build --dir cov-int make
VERSION=$(git describe --tags --abbrev=1 --dirty=-dev --always)
tar -czf ${VERSION}-cov.tgz cov-int
echo -e "\nUpload: ${VERSION}-cov.tgz\n"
