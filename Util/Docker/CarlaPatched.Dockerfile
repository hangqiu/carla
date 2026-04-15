ARG UBUNTU_DISTRO="20.04"

# ── Stage 1: Build UE4 ────────────────────────────────────────────────────
FROM carla-base:ue4-${UBUNTU_DISTRO} AS ue4_builder

ARG UID="1000"
ARG GID="1000"
ARG DOCKER_GID="999"
ARG USERNAME="carla"

ENV DEBIAN_FRONTEND=noninteractive

RUN id -u ${UID} &>/dev/null \
    && userdel -r $(getent passwd ${UID} | cut -d: -f1) \
    || echo ""

RUN groupadd --gid ${GID} ${USERNAME} \
    && useradd -m --uid ${UID} -g ${USERNAME} ${USERNAME} \
    && passwd -d ${USERNAME} \
    && usermod -a -G sudo ${USERNAME}

# optional: add to docker group (needed only if building within the container)
RUN groupadd -g ${DOCKER_GID} docker \
    && usermod -a -G docker ${USERNAME} 2>/dev/null || true

USER ${USERNAME}
ENV HOME="/home/${USERNAME}"
WORKDIR /workspaces

# Clone UE4 using SSH agent forwarding (no hardcoded credentials)
ENV UE4_ROOT="/workspaces/UnrealEngine"
RUN --mount=type=ssh \
    GIT_SSH_COMMAND="ssh -o StrictHostKeyChecking=no" \
    git clone --depth 1 -b carla \
    git@github.com:CarlaUnreal/UnrealEngine.git ${UE4_ROOT}

WORKDIR ${UE4_ROOT}
RUN ./Setup.sh
RUN ./GenerateProjectFiles.sh
RUN make -j$(nproc)

# ── Stage 2: Build CARLA from the patched source ──────────────────────────
FROM ue4_builder AS carla_builder

ENV UE4_ROOT="/workspaces/UnrealEngine"
ENV CARLA_ROOT="/workspaces/carla"

WORKDIR /workspaces

# Copy patched CARLA source from build context
COPY --chown=${USERNAME}:${USERNAME} . ${CARLA_ROOT}

WORKDIR ${CARLA_ROOT}

RUN ./Update.sh
RUN make PythonAPI
RUN make CarlaUE4Editor
# Package and rename to a fixed path so the runtime stage can copy it
RUN make package && \
    pkg=$(find Dist -maxdepth 1 -mindepth 1 -type d | head -1) && \
    echo "Package: $pkg" && \
    mv "$pkg" /tmp/carla_release

# ── Stage 3: Minimal runtime image ────────────────────────────────────────
FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN useradd -m carla

WORKDIR /workspace
COPY --from=carla_builder --chown=carla:carla /tmp/carla_release/ .

RUN apt-get update && apt-get install -y \
    libsdl2-2.0 xserver-xorg libvulkan1 libomp5 xdg-user-dirs \
    && rm -rf /var/lib/apt/lists/*

ENV OMP_PROC_BIND="FALSE"
ENV OMP_NUM_THREADS="48"
ENV NVIDIA_DRIVER_CAPABILITIES="all"
ENV NVIDIA_VISIBLE_DEVICES="all"

USER carla
CMD /bin/bash CarlaUE4.sh
