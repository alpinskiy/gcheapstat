ARG TAG
FROM mcr.microsoft.com/dotnet/runtime-deps:$TAG
RUN apt-get update &&\
 apt-get install -y --no-install-recommends\
 curl xz-utils make clang &&\
 rm -rf /var/lib/apt/lists/*
RUN curl -s 'https://cmake.org/files/v3.20/cmake-3.20.5-linux-x86_64.tar.gz' |\
 tar --strip-components=1 -xzC /usr/local
ARG UNAME=user
ARG UID
ARG GID
RUN addgroup --gid $GID $UNAME
RUN adduser --disabled-password --gecos '' --uid $UID --gid $GID $UNAME
USER $UNAME
