FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONUNBUFFERED=1
ENV WORKSPACE_DIR=/workspace/auracash

# Install core build toolchains, cryptographic libraries, event loops, and networking utilities
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    clang \
    gdb \
    valgrind \
    git \
    curl \
    wget \
    jq \
    ufw \
    python3 \
    python3-pip \
    python3-venv \
    libssl-dev \
    libcurl4-openssl-dev \
    libsqlite3-dev \
    libsecp256k1-dev \
    libboost-all-dev \
    libevent-dev \
    nlohmann-json3-dev \
    pkg-config \
    ca-certificates \
    iproute2 \
    net-tools \
    && rm -rf /var/lib/apt/lists/*

# Set up workspace
WORKDIR /workspace/auracash

COPY requirements.txt .
RUN pip3 install --break-system-packages -r requirements.txt

# Copy workspace files
COPY . .

CMD ["python3", "controller.py"]
