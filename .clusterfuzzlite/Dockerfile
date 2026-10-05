FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:5add54bcdde77ae3427025db959a44f379ca64b6332bcddcc2766924582e47d9

# Install vcpkg dependencies and build tools.
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    ninja-build \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    git \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg.
ENV VCPKG_ROOT=/vcpkg
RUN git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT" \
    && git -C "$VCPKG_ROOT" checkout 401bb04e16e1ae03fdbec04edea4e52c93e9aeb5 \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

COPY . $SRC/klspw
WORKDIR $SRC/klspw
COPY .clusterfuzzlite/build.sh $SRC/
