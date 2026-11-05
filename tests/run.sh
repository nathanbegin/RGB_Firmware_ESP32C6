#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_bin=$(mktemp)
trap 'rm -f "$test_bin"' EXIT
cc -std=c11 -Wall -Wextra -Werror -pedantic -I main main/light_engine.c tests/test_light_engine.c -lm -o "$test_bin"
"$test_bin"
node --check esp_c6_unit02.mjs
cc -std=c11 -Wall -Wextra -Werror -pedantic -I tests/ota_stubs -I main main/ota_client.c tests/test_ota_client.c -o "$test_bin"
"$test_bin"
python3 tests/test_ota_package.py
