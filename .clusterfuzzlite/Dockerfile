FROM gcr.io/oss-fuzz-base/base-builder:v1

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
RUN git clone --depth 1 --branch 2026.03.18 https://github.com/microsoft/vcpkg "$VCPKG_ROOT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

COPY . $SRC/klspw
WORKDIR $SRC/klspw
COPY .clusterfuzzlite/build.sh $SRC/
