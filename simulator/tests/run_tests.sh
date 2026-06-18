#!/bin/bash
set -e
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
FLAGS="-std=c++17 -Wall -Wextra -Ilib"
SRCS="lib/packet.cpp lib/mac.cpp lib/nonce_buf.cpp lib/platform_node.cpp lib/home_node.cpp"
PASS=0; FAIL=0

run_test() {
    local name="$1"; local src="$2"
    local bin="build/test_${name}"
    if $CXX $FLAGS $SRCS "tests/${src}" -pthread -o "$bin" 2>&1; then
        if "$bin"; then
            echo "  [PASS] $name"
            PASS=$((PASS+1))
        else
            echo "  [FAIL] $name (runtime)"
            FAIL=$((FAIL+1))
        fi
    else
        echo "  [FAIL] $name (compile)"
        FAIL=$((FAIL+1))
    fi
}

mkdir -p build
echo "=== saftybox simulator tests ==="
run_test packet      test_packet.cpp
run_test mac         test_mac.cpp
run_test nonce_buf   test_nonce_buf.cpp
run_test platform    test_platform_node.cpp
run_test home        test_home_node.cpp
run_test integration test_integration.cpp

echo ""
echo "=== $PASS passed, $FAIL failed ==="
[ $FAIL -eq 0 ]
