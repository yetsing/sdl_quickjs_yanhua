#!/bin/bash

cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -S . -B build
cmake --build build
./build/yanhua
cp build/compile_commands.json .  # clangd
