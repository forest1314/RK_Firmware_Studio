#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

CPP_BUILD_DIR="$ROOT_DIR/build"
CPP_BIN="$CPP_BUILD_DIR/rkstudio_cpp"

build_cpp() {
  cmake -S "$ROOT_DIR" -B "$CPP_BUILD_DIR"
  cmake --build "$CPP_BUILD_DIR" -j"$(nproc)"
}

if [[ "${1:-}" == "--help" ]]; then
  cat <<'EOF'
RK Firmware Studio

Usage:
  ./run_rkstudio.sh             Build and launch the native Qt C++ UI
  ./run_rkstudio.sh --web       Launch the C++ local web UI fallback
  ./run_rkstudio.sh --self-test Run native C++ smoke tests
  ./run_rkstudio.sh --help      Show this help
EOF
  exit 0
fi

if [[ "${1:-}" == "--self-test" ]]; then
  build_cpp
  "$CPP_BIN" --smoke-test
  exec env QT_QPA_PLATFORM=offscreen "$CPP_BIN" --qt-smoke-test
fi

build_cpp
if [[ "${1:-}" == "--web" ]]; then
  exec "$CPP_BIN" --web
fi
exec "$CPP_BIN"
