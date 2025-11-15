#!/bin/bash
set -e

PROTO_SRC=../../protos/playout/playout.proto
OUT_CPP=../src/generated

mkdir -p $OUT_CPP

protoc \
  --proto_path=../../protos/playout \
  --cpp_out=$OUT_CPP \
  --grpc_out=$OUT_CPP \
  --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` \
  $PROTO_SRC

echo "Generated C++ gRPC bindings in $OUT_CPP"
