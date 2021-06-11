#!/usr/bin/env bash

echo "#define BUILD $APPVEYOR_BUILD_NUMBER" > build_number.txt
./build.sh Release 5.0-buster-slim-amd64
python appveyor/artifacts.py
