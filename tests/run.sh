#!/bin/sh
screw=$1
exes=$2
expected=$3
pass=0
fail=0

for exp in "$expected"/*.out; do
    name=$(basename "$exp" .out)
    exe="$exes/$name.exe"
    args=""
    [ -f "$expected/$name.args" ] && args=$(cat "$expected/$name.args")
    code=0
    out=$(eval timeout 20 "$screw" "$exe" $args 2>&1) || code=$?
    want_code=0
    [ -f "$expected/$name.code" ] && want_code=$(cat "$expected/$name.code")
    if [ "$out" = "$(cat "$exp")" ] && [ "$code" = "$want_code" ]; then
        pass=$((pass + 1))
        echo "ok    $name"
    else
        fail=$((fail + 1))
        echo "FAIL  $name (exit $code, want $want_code)"
        printf '%s\n' "$out" | diff -u "$exp" - | sed 's/^/      /' | head -20
    fi
done

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
