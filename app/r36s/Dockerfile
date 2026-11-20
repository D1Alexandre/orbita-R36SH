# The static WebRTC dependencies are cached independently of application edits.
FROM ubuntu:20.04 AS webrtc
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential git python3 ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY deps/webrtc-xenon deps/webrtc-xenon
COPY deps/webrtc-linux deps/webrtc-linux
RUN OUTROOT=/webrtc bash deps/webrtc-linux/build.sh

# Ubuntu 20.04 baseline for older ArkOS images. Build for the device's architecture.
FROM ubuntu:20.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config libsdl2-dev libcurl4-openssl-dev \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
    python3 openssl ca-certificates && rm -rf /var/lib/apt/lists/*
RUN apt-get update && apt-get install -y --no-install-recommends libopus-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY app/r36s app/r36s
COPY platform/linux platform/linux
COPY app/h264play/source/login app/h264play/source/login
COPY app/h264play/source/ui app/h264play/source/ui
COPY app/h264play/source/session app/h264play/source/session
COPY app/h264play/source/webrtc app/h264play/source/webrtc
COPY app/h264play/hosttest/session app/h264play/hosttest/session
COPY --from=webrtc /webrtc/host /opt/orbita-webrtc
RUN cmake -S app/r36s -B /build -DCMAKE_BUILD_TYPE=Release -DORBITA_WEBRTC_ROOT=/opt/orbita-webrtc \
    && cmake --build /build -j2 \
    && cd /build && ctest --output-on-failure \
    && cmake --install /build --prefix /stage \
    && sed -i 's/\r$//' /stage/Orbita.sh \
    && cp -r /opt/orbita-webrtc/licenses /stage/orbita/ \
    && cp /opt/orbita-webrtc/BUILDINFO.txt /stage/orbita/WEBRTC-BUILDINFO.txt \
    && /build/orbita --diagnose > /stage/orbita/BUILDINFO.txt \
    && readelf -h /build/orbita >> /stage/orbita/BUILDINFO.txt
COPY LICENSE /stage/orbita/LICENSE
COPY THIRD_PARTY.md /stage/orbita/THIRD_PARTY.md
COPY third_party/inter-4.1/OFL.txt /stage/orbita/Inter-OFL.txt
COPY app/h264play/source/login/cjson/LICENSE /stage/orbita/cJSON-LICENSE
COPY app/r36s/README.md /stage/orbita/README.md

FROM scratch AS package
COPY --from=build /stage/ /
