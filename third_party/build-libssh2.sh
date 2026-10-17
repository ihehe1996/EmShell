#!/usr/bin/env bash
#
# 编译 libssh2 静态库，产物装到 third_party/libssh2/。
#
# 为什么用 WinCNG 后端：WinCNG 是 Windows 自带的加密 API（bcrypt.dll），
# 这样 libssh2 就不需要 OpenSSL 或 mbedTLS，依赖面小很多，也不会因为
# DLL 版本问题在分发时踩坑。
#
# 这个脚本只在需要重建库的时候跑一次，产物已经在仓库里了(见 .gitignore 说明)。
#
# 用法：
#   bash third_party/build-libssh2.sh [下载缓存目录]
#
set -euo pipefail

LIBSSH2_VERSION="1.11.1"
TARBALL="libssh2-${LIBSSH2_VERSION}.tar.gz"
URL="https://codeload.github.com/libssh2/libssh2/tar.gz/refs/tags/libssh2-${LIBSSH2_VERSION}"

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="${1:-${TMPDIR:-/tmp}/emshell-libssh2-build}"

QT_BIN="D:/Program Files/QT/6.8.3/mingw_64/bin"
MINGW_BIN="D:/Program Files/QT/Tools/mingw1310_64/bin"
CMAKE_BIN="D:/Program Files/QT/Tools/CMake_64/bin"
NINJA_BIN="D:/Program Files/QT/Tools/Ninja"

export PATH="${QT_BIN}:${MINGW_BIN}:${CMAKE_BIN}:${NINJA_BIN}:${PATH}"

echo "工作目录: ${WORK_DIR}"
mkdir -p "${WORK_DIR}/dl" "${WORK_DIR}/src" "${WORK_DIR}/build"

# ---- 1. 下载 ----
if [ ! -f "${WORK_DIR}/dl/${TARBALL}" ]; then
    echo "==> 下载 libssh2 ${LIBSSH2_VERSION}"
    curl -sSL -o "${WORK_DIR}/dl/${TARBALL}" "${URL}"
else
    echo "==> 已有下载缓存，跳过"
fi

# ---- 2. 解压到独立的空目录 ----
if [ ! -d "${WORK_DIR}/src/libssh2-libssh2-${LIBSSH2_VERSION}" ]; then
    echo "==> 解压"
    tar -xzf "${WORK_DIR}/dl/${TARBALL}" -C "${WORK_DIR}/src"
fi
SRC_DIR="${WORK_DIR}/src/libssh2-libssh2-${LIBSSH2_VERSION}"

# ---- 3. 配置 ----
echo "==> 配置（静态库 + WinCNG）"
cmake -G Ninja -S "${SRC_DIR}" -B "${WORK_DIR}/build" \
    -DCMAKE_C_COMPILER="${MINGW_BIN}/gcc.exe" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PROJECT_ROOT}/third_party/libssh2" \
    -DCRYPTO_BACKEND=WinCNG \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTING=OFF \
    -DENABLE_ZLIB_COMPRESSION=OFF

# ---- 4. 编译安装 ----
echo "==> 编译"
cmake --build "${WORK_DIR}/build" --config Release

echo "==> 安装到 third_party/libssh2"
cmake --install "${WORK_DIR}/build" --config Release

echo
echo "完成。产物："
find "${PROJECT_ROOT}/third_party/libssh2" -type f | sed "s|${PROJECT_ROOT}/||"
