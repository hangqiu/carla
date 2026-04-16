# Minimal CARLA runtime image.
# Build context must be the packaged CARLA release directory
# (the output of `make package`, i.e. the CarlaUE4/ folder).
FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN useradd -m carla

WORKDIR /workspace
COPY --chown=carla:carla . .

RUN apt-get update && apt-get install -y \
    libsdl2-2.0 xserver-xorg libvulkan1 libomp5 xdg-user-dirs \
    && rm -rf /var/lib/apt/lists/*

ENV OMP_PROC_BIND="FALSE"
ENV OMP_NUM_THREADS="48"
ENV NVIDIA_DRIVER_CAPABILITIES="all"
ENV NVIDIA_VISIBLE_DEVICES="all"

USER carla
CMD ["/bin/bash", "CarlaUE4.sh"]
