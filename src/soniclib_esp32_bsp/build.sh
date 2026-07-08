#!/bin/sh

# build the project
docker run --rm -v $(pwd)/src:/project -w /project -u $UID -e HOME=/tmp espressif/idf idf.py build
