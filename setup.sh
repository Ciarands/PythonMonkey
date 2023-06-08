#!/bin/bash
set -euo pipefail
IFS=$'\n\t'


# Get number of CPU cores
CPUS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || getconf NPROCESSORS_ONLN 2>/dev/null || echo 1)

echo "Installing dependencies"
sudo apt-get update --yes
sudo apt-get upgrade --yes
sudo apt-get install cmake doxygen graphviz gcovr llvm g++ pkg-config m4 --yes
sudo curl --proto '=https' --tlsv1.3 https://sh.rustup.rs -sSf | sh -s -- -y #install rust compiler
echo "Done installing dependencies"

echo "Downloading spidermonkey source code"
wget -c -q https://ftp.mozilla.org/pub/firefox/releases/102.11.0esr/source/firefox-102.11.0esr.source.tar.xz
mkdir -p firefox-source
tar xf firefox-102.11.0esr.source.tar.xz -C firefox-source --strip-components=1 # strip the root folder
echo "Done downloading spidermonkey source code"

echo "Building spidermonkey"
cd firefox-source/js
sed -i 's/bool Unbox/JS_PUBLIC_API bool Unbox/g' ./public/Class.h           # need to manually add JS_PUBLIC_API to js::Unbox until it gets fixed in Spidermonkey
sed -i 's/bool js::Unbox/JS_PUBLIC_API bool js::Unbox/g' ./src/vm/JSObject.cpp  # same here
cd src
cp ./configure.in ./configure
chmod +x ./configure
mkdir -p _build
cd _build
../configure \
  --prefix=$(realpath $PWD/../../../../_spidermonkey_install) \
  --with-intl-api \
  --with-system-zlib \
  --disable-jemalloc \
  --disable-debug-symbols \
  --disable-js-shell --disable-tests \
  --enable-optimize 
make -j$CPUS
echo "Done building spidermonkey"

echo "Installing spidermonkey"
mkdir -p ../../../../_spidermonkey_install/
make install 
echo "Done installing spidermonkey"
