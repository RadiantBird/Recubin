#!/bin/bash
# src/echo_server.cpp を assets/echo_server_mac にビルドする。
set -e
cd "$(dirname "$0")"
mkdir -p assets
clang++ -std=c++17 -O2 src/echo_server.cpp -o assets/echo_server_mac
echo "Built assets/echo_server_mac"
