FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      libgl1-mesa-dev \
      libssl3 \
      libusb-1.0-0 \
      libqt5charts5-dev \
      libqt5sql5-sqlite \
      qtbase5-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
CMD ["bash", "scripts/build_linux.sh"]
