#!/usr/bin/env bash
#
# Packages a runnable Sencha game: the host binary, a game module, cooked
# content, and the shared libraries a clean machine will not have. Two flavours
# come out of the same content -- a dedicated server that runs headless and
# hosts, and a client that joins one -- because that is the pair a session
# needs and building them separately is how they drift apart.
#
# The bundle is self-contained apart from the platform's graphics driver: it
# carries its own SDL and engine libraries and a launcher that points the loader
# at them, which is what makes "copy the directory to the other laptop" work.
#
#   scripts/package_bundle.sh --content <dir> --map <levels/name> --out <dir>
#
# Linux only for now. Windows bundles ride the packaging work in Track F.

set -euo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
CONTENT_DIR=""
GAME_MODULE=""
MAP=""
OUT_DIR=""
PORT="27500"
NAME=""

usage()
{
    cat >&2 <<'USAGE'
Usage: package_bundle.sh --content <dir> --map <levels/name> --out <dir> [options]

  --content <dir>   Project content root (the directory holding assets/).
  --map <name>      Level the server hosts, e.g. levels/EntranceHall.
  --out <dir>       Directory to write the bundles into.
  --module <path>   Game module .so (default: the built template module).
  --build <dir>     Build tree to take binaries from (default: <repo>/build).
  --port <n>        UDP port the server binds (default: 27500).
  --name <text>     Bundle name prefix (default: the content directory's name).
USAGE
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --content) CONTENT_DIR="${2:-}"; shift 2 ;;
        --map)     MAP="${2:-}"; shift 2 ;;
        --out)     OUT_DIR="${2:-}"; shift 2 ;;
        --module)  GAME_MODULE="${2:-}"; shift 2 ;;
        --build)   BUILD_DIR="${2:-}"; shift 2 ;;
        --port)    PORT="${2:-}"; shift 2 ;;
        --name)    NAME="${2:-}"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "unknown option: $1" >&2; usage ;;
    esac
done

[[ -n "$CONTENT_DIR" && -n "$MAP" && -n "$OUT_DIR" ]] || usage

CONTENT_DIR="$(cd -- "$CONTENT_DIR" && pwd)"
[[ -d "$CONTENT_DIR/assets" ]] || { echo "no assets/ under '$CONTENT_DIR'" >&2; exit 1; }
[[ -d "$CONTENT_DIR/assets/.cooked" ]] || {
    echo "no assets/.cooked under '$CONTENT_DIR' -- cook the project first" >&2
    exit 1
}

APP_BIN="$BUILD_DIR/app/app"
[[ -x "$APP_BIN" ]] || { echo "host binary not found at '$APP_BIN'" >&2; exit 1; }

if [[ -z "$GAME_MODULE" ]]; then
    GAME_MODULE="$REPO_ROOT/template/build/game.so"
fi
[[ -f "$GAME_MODULE" ]] || { echo "game module not found at '$GAME_MODULE'" >&2; exit 1; }

[[ -n "$NAME" ]] || NAME="$(basename -- "$CONTENT_DIR")"
PLATFORM="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"

mkdir -p "$OUT_DIR"

# The shared libraries a clean machine will not already have. System libraries
# (glibc, libstdc++, the graphics driver stack) are deliberately left behind:
# copying them is what makes a bundle fail on a machine slightly unlike the one
# that built it.
copy_runtime_libraries()
{
    local dest="$1"
    mkdir -p "$dest"
    ldd "$APP_BIN" | awk '/=> \//{print $3}' | while read -r lib; do
        case "$(basename -- "$lib")" in
            libSDL3*|libsencha_*|libvulkan.so*)
                # libvulkan is the loader, not a driver, and is matched by name
                # only so a machine without the package still starts.
                [[ "$(basename -- "$lib")" == libvulkan.so* ]] && continue
                cp -L "$lib" "$dest/"
                ;;
        esac
    done
}

write_bundle()
{
    local kind="$1"      # server | client
    local dir="$OUT_DIR/$NAME-$kind-$PLATFORM"

    rm -rf "$dir"
    mkdir -p "$dir/bin" "$dir/licenses"

    cp "$APP_BIN" "$dir/bin/app"
    cp "$GAME_MODULE" "$dir/bin/game.so"
    copy_runtime_libraries "$dir/lib"

    # Content, cooked output included: a bundle that has to be cooked on the
    # target machine is not a bundle.
    cp -r "$CONTENT_DIR/assets" "$dir/assets"

    find "$REPO_ROOT/third_party" -maxdepth 2 -iname "LICENSE*" 2>/dev/null | while read -r license; do
        cp "$license" "$dir/licenses/$(basename -- "$(dirname -- "$license")")-LICENSE.txt" 2>/dev/null || true
    done

    if [[ "$kind" == "server" ]]; then
        cat > "$dir/run-server.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail
bundle_dir="\$(cd -- "\$(dirname -- "\${BASH_SOURCE[0]}")" && pwd)"
cd "\$bundle_dir"

export LD_LIBRARY_PATH="\$bundle_dir/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"

echo "Sencha dedicated server: $MAP on UDP port $PORT"
echo "Give players this machine's address:"
hostname -I 2>/dev/null || true
echo "Type 'quit' or press Ctrl-C to stop."

exec "\$bundle_dir/bin/app" --headless \\
    --game "\$bundle_dir/bin/game.so" \\
    +map $MAP \\
    +host $PORT
EOF
        chmod +x "$dir/run-server.sh"

        cat > "$dir/README.txt" <<EOF
$NAME dedicated server ($PLATFORM)
$(printf '=%.0s' $(seq 1 $((${#NAME} + 26 + ${#PLATFORM}))))

Runs $MAP and hosts a session on UDP port $PORT. No window and no graphics
driver needed: this process simulates the world and nobody plays in it.

    ./run-server.sh

Stop it by typing 'quit' at its terminal, or with Ctrl-C. Both shut the session
down properly and tell the connected players.

The terminal is also the server's console: type any engine command there, for
example 'net.max_peers 8'.

If a firewall is in the way, allow the port for this boot:

    sudo firewall-cmd --add-port=$PORT/udp

Players run the client bundle and connect to this machine's address. Both ends
must be built from the same source: a mismatch is refused at the handshake
rather than desyncing later.
EOF
    else
        cat > "$dir/run-client.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail
bundle_dir="\$(cd -- "\$(dirname -- "\${BASH_SOURCE[0]}")" && pwd)"
cd "\$bundle_dir"

if (( \$# > 1 )); then
    echo "Usage: \$0 [server-address[:port]]" >&2
    exit 2
fi

server="\${1:-}"
if [[ -z "\$server" ]]; then
    read -r -p "Server address: " server
fi
[[ -n "\$server" ]] || { echo "A server address is required." >&2; exit 2; }
[[ "\$server" == *:* ]] || server="\$server:$PORT"

export LD_LIBRARY_PATH="\$bundle_dir/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"

echo "Connecting to \$server ..."
exec "\$bundle_dir/bin/app" \\
    --game "\$bundle_dir/bin/game.so" \\
    +connect "\$server"
EOF
        chmod +x "$dir/run-client.sh"

        cat > "$dir/README.txt" <<EOF
$NAME client ($PLATFORM)
$(printf '=%.0s' $(seq 1 $((${#NAME} + 10 + ${#PLATFORM}))))

Joins a session hosted by the server bundle. The server tells this process
which world to load, so there is nothing to choose here.

    ./run-client.sh <server-address>

Requires a working Vulkan driver. Both ends must be built from the same source:
a mismatch is refused at the handshake rather than desyncing later.
EOF
    fi

    echo "$dir"
}

server_dir="$(write_bundle server)"
client_dir="$(write_bundle client)"

echo "packaged:"
echo "  $server_dir"
echo "  $client_dir"
