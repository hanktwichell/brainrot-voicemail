#!/bin/bash
# Builds the backend for Linux (no AppKit/Speech/AVFoundation) and runs the UI flow in headless Chrome.
# Needs: gcc with ASan, sqlite3 dev headers, ffmpeg, google-chrome, python3 + playwright.
set -e
cd "$(dirname "$0")/.."
python3 build.py >/dev/null            # regenerates src/html.c
D="-DBV_LINUX_TEST -Dposix_spawn_file_actions_init=bv_fa_init -Dposix_spawn_file_actions_destroy=bv_fa_destroy \
   -Dposix_spawn_file_actions_adddup2=bv_fa_dup2 -Dposix_spawn_file_actions_addclose=bv_fa_close \
   -Dposix_spawn_file_actions_addopen=bv_fa_open -Dposix_spawn=bv_spawn"
mkdir -p out
gcc -O1 -g -fsanitize=address -w $D -c src/main.c -o out/test_main.o
gcc -c src/html.c -o out/test_html.o
gcc -fsanitize=address -g tests/linux_harness.c out/test_main.o out/test_html.o -lsqlite3 -lpthread -o out/bvtest
echo "Built out/bvtest. Run it with HOME pointing at a fake home containing Library/Messages/chat.db,"
echo "then: python3 tests/ui_test.py (reads the port from /tmp/bvport.txt)."
