FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      libgl1-mesa-dev \
      libssl-dev \
      libusb-1.0-0 \
      libqt5charts5-dev \
      libqt5sql5-sqlite \
      ninja-build \
      python3-pip \
      qtbase5-dev \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m pip install --no-cache-dir cmake==3.31.6

WORKDIR /src
CMD ["bash", "scripts/build_linux.sh"]
