#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"

echo "==> AuraCash installer"
echo "==> Cleaning stale build caches..."
rm -rf "${BUILD_DIR}"

echo "==> Configuring CMake..."
cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release

echo "==> Building binaries..."
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "==> Installing binaries to /usr/local/bin ..."
install -D -m 0755 "${BUILD_DIR}/auracash-node"   /usr/local/bin/auracash-node
install -D -m 0755 "${BUILD_DIR}/auracash-wallet" /usr/local/bin/auracash-wallet
install -D -m 0755 "${BUILD_DIR}/auracash-miner"  /usr/local/bin/auracash-miner

echo "==> Creating /usr/local/bin/auracash CLI wrapper ..."
cat > /usr/local/bin/auracash <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<USAGE
Usage: auracash <command> [args]

Commands:
  node                  Run the AuraCash node daemon
  miner [host] [port] [address]  Run standalone miner
  wallet generatekey    Generate a new wallet key pair
  wallet getbalance     Get wallet balance via node RPC
USAGE
}

if [ $# -lt 1 ]; then
    usage
    exit 1
fi

cmd="$1"; shift

case "$cmd" in
    node)
        exec auracash-node "$@"
        ;;
    miner)
        exec auracash-miner "$@"
        ;;
    wallet)
        if [ $# -lt 1 ]; then
            echo "Error: wallet subcommand required" >&2
            usage
            exit 1
        fi
        sub="$1"; shift
        case "$sub" in
            generatekey)
                exec auracash-wallet generatekey "$@"
                ;;
            getbalance)
                exec auracash-wallet getbalance "$@"
                ;;
            *)
                echo "Error: unknown wallet subcommand '$sub'" >&2
                usage
                exit 1
                ;;
        esac
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        echo "Error: unknown command '$cmd'" >&2
        usage
        exit 1
        ;;
esac
EOF
chmod +x /usr/local/bin/auracash

echo "==> Installation complete."
echo "    Binaries: /usr/local/bin/auracash-node, auracash-wallet, auracash-miner"
echo "    Wrapper:  /usr/local/bin/auracash"
