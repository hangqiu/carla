ARG UBUNTU_DISTRO="20.04"
# Limit parallel jobs to avoid OOM during UE4/CARLA link phases.
# UE4 lld link jobs spike 2-4 GB each; default is nproc/4 (=8 on 32-core/32GB).
ARG BUILD_JOBS="8"

# ── Stage 1: Build UE4 ────────────────────────────────────────────────────
# Tagged as carla-ue4-builder:latest. Never rebuilt unless you remove that image.
FROM carla-base:ue4-${UBUNTU_DISTRO} AS ue4_builder

ARG BUILD_JOBS
ENV UE4_ROOT="/workspaces/UnrealEngine"
WORKDIR /workspaces

RUN --mount=type=ssh \
    GIT_SSH_COMMAND="ssh -o StrictHostKeyChecking=no" \
    git clone --depth 1 -b carla \
    git@github.com:CarlaUnreal/UnrealEngine.git ${UE4_ROOT}

WORKDIR ${UE4_ROOT}
RUN ./Setup.sh
RUN ./GenerateProjectFiles.sh
# UnrealHeaderTool must be built first with -j1: its link phase OOMs at -j8
# because lld spikes 2-4 GB per job and HeaderTool has ~38 concurrent link steps.
RUN make -j1 UnrealHeaderTool && \
    make -j${BUILD_JOBS} UE4Editor ShaderCompileWorker

# ── Stage 2: Download CARLA content assets ────────────────────────────────
# Tagged as carla-content:latest. Only rebuilds if Update.sh or Util/ changes.
FROM carla-ue4-builder:latest AS content_builder

ENV CARLA_ROOT="/workspaces/carla"
WORKDIR ${CARLA_ROOT}

COPY Update.sh .
COPY Util/ Util/
RUN ./Update.sh

# ── Stage 3: Compile CARLA (PythonAPI + CarlaUE4Editor) ───────────────────
# Tagged as carla-compiled:latest. Rebuilds only when source code changes.
# Changing Package.sh alone does NOT invalidate this stage.
FROM carla-content:latest AS compiled_builder

ENV UE4_ROOT="/workspaces/UnrealEngine"
ENV CARLA_ROOT="/workspaces/carla"

WORKDIR /workspaces
COPY . ${CARLA_ROOT}

WORKDIR ${CARLA_ROOT}
# carla/ is a git submodule — .git is a file pointing to parent .git/modules/carla
# which doesn't exist in Docker. Replace with a real repo so git commands work.
RUN rm -f .git \
    && git init \
    && git -c user.email="build@docker" -c user.name="build" commit --allow-empty -m "build"

RUN make PythonAPI
RUN make CarlaUE4Editor
