#!/usr/bin/env bash
set -ex

# This script downloads, builds, and installs a particular version of gRPC
# from source including all of its dependencies. It will install the resulting
# static libraries into the prefix passed on the command line. It is loosely
# based on this script:
#   https://github.com/grpc/grpc/blob/460898f11c464d66157ce539a22d71c896500444/test/distrib/cpp/run_distrib_test_cmake.sh

if [ "$#" -ne 1 ]; then
  echo "missing required parameter for install prefix"
  exit 1
fi

GRPC_INSTALL_PREFIX=$1
GRPC_URI="http://github.com/grpc/grpc"
GRPC_TAG="460898f11c464d66157ce539a22d71c896500444"
WORK_DIR=$(mktemp -d -t grpc-XXXXXXXXXX)
PARALLEL_JOBS=20

# ensure we are using binaries from our own prefix for all builds here
export PATH="$GRPC_INSTALL_PREFIX/bin:$PATH"

# install openssl (to use instead of boringssl)
apt-get update && apt-get install -y libssl-dev

# download the gRPC source to the work directory
# NOTE: can't use a tarball download because it won't include git submodules
# wget -c "$GRPC_URI/archive/$GRPC_TAG.tar.gz" -O - | tar -xz --strip-components 1 -C $WORK_DIR
git clone --depth 1 $GRPC_URI $WORK_DIR
pushd $WORK_DIR
git reset --hard $GRPC_TAG
git submodule update --init

# install absl
mkdir -p "third_party/abseil-cpp/cmake/build"
pushd "third_party/abseil-cpp/cmake/build"
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE \
  -DCMAKE_INSTALL_PREFIX=$GRPC_INSTALL_PREFIX \
  ../..
make -j${PARALLEL_JOBS} install
popd

# install c-ares
mkdir -p "third_party/cares/cares/cmake/build"
pushd "third_party/cares/cares/cmake/build"
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=$GRPC_INSTALL_PREFIX \
  ../..
make -j${PARALLEL_JOBS} install
popd

# install protobuf
mkdir -p "third_party/protobuf/cmake/build"
pushd "third_party/protobuf/cmake/build"
cmake \
  -Dprotobuf_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=$GRPC_INSTALL_PREFIX \
  ..
make -j${PARALLEL_JOBS} install
popd

# install re2
mkdir -p "third_party/re2/cmake/build"
pushd "third_party/re2/cmake/build"
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE \
  -DCMAKE_INSTALL_PREFIX=$GRPC_INSTALL_PREFIX \
  ../..
make -j${PARALLEL_JOBS} install
popd

# install zlib
mkdir -p "third_party/zlib/cmake/build"
pushd "third_party/zlib/cmake/build"
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$GRPC_INSTALL_PREFIX ../..
make -j${PARALLEL_JOBS} install
popd

# install gRPC
mkdir -p "cmake/build"
pushd "cmake/build"
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DgRPC_INSTALL=ON \
  -DgRPC_BUILD_TESTS=OFF \
  -DgRPC_CARES_PROVIDER=package \
  -DgRPC_ABSL_PROVIDER=package \
  -DgRPC_PROTOBUF_PROVIDER=package \
  -DgRPC_RE2_PROVIDER=package \
  -DgRPC_SSL_PROVIDER=package \
  -DgRPC_ZLIB_PROVIDER=package \
  -DCMAKE_PREFIX=$GRPC_INSTALL_PREFIX \
  -DCMAKE_INSTALL_PREFIX=$GRPC_INSTALL_PREFIX \
  ../..
make -j${PARALLEL_JOBS} install
popd

# clean up the work dir now that we've installed
rm -rf $WORK_DIR
