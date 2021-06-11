#!/usr/bin/env bash

if [ "$1" = "/?" ]; then
  echo "Usage:"
  echo "  build.sh Configuration AspNetRuntimeDockerImageTag"
  echo
  echo "  Configuration                'Debug' (default) or 'Release'"
  echo "  AspNetRuntimeDockerImageTag  i.e. '5.0-buster-slim-amd64', see full listing at"
  echo "                               https://hub.docker.com/_/microsoft-dotnet-aspnet"
  exit 1
fi

Configuration=$1
AspNetRuntimeDockerImageTag=$2

if [ -z "$Configuration" ]; then
  Configuration=Debug
fi

if [ "$DOTNET_RUNNING_IN_CONTAINER" == "true" ]; then
  # Running in docker container
  CMakeBuildDir=$(dirname "$0")/build/$AspNetRuntimeDockerImageTag/$Configuration
else
  # Running on host machine
  if [ -z "$AspNetRuntimeDockerImageTag" ]; then
    CMakeBuildDir=$(dirname "$0")/build/linux/$Configuration
  else
    # Build docker image
    Name=gcheapstat-$AspNetRuntimeDockerImageTag
    docker build -t $Name\
      --build-arg UID=$(id -u) --build-arg GID=$(id -g)\
      --build-arg TAG=$AspNetRuntimeDockerImageTag .
    test $? -eq 0 || exit 1
    # Run build in docker container
    tty=""
    if [ -z "$CI" ]; then
      tty=" -it"
    fi
    docker run$tty --rm \
      --user "$(id -u):$(id -g)" --mount src="$(pwd)",target=/src,type=bind \
      --entrypoint /src/build.sh $Name $Configuration $AspNetRuntimeDockerImageTag
    exit
  fi
fi

# Build
CMakeSourceDir=$(dirname "$0")
export CC=/usr/bin/clang
export CXX=/usr/bin/clang++
cmake -S "$CMakeSourceDir" -B "$CMakeBuildDir" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=$Configuration
test $? -eq 0 || exit 1
cmake --build "$CMakeBuildDir" --config $Configuration
