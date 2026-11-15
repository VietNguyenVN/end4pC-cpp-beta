#!/usr/bin/env bash
# Translates between the two languages chosen in the Tools translator, detecting which one was typed.
# Usage: translate.sh TEXT [SRC] [TGT]   (SRC may be "auto"; with no languages: English <-> Spanish)
# Prints: <source>\t<target>\t<translation>
text="$1"
[ -z "$text" ] && exit 1
a="${2:-en}"
b="${3:-es}"
[ -z "$b" ] || [ "$b" = "auto" ] && b="es"

det=$(trans -id -- "$text" 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | awk '$1 == "Code" { print $2; exit }')

if [ "$a" = "auto" ]; then
    # The source is whatever was typed; it goes to the target, or to English when it already is the target.
    src="${det:-auto}"
    tgt="$b"
    if [ "$det" = "$b" ]; then
        [ "$b" = "en" ] && tgt="es" || tgt="en"
    fi
elif [ "$det" = "$b" ]; then
    # Typed in the target language: it goes back to the source one.
    src="$b"
    tgt="$a"
else
    src="$a"
    tgt="$b"
    # Not the source language either (short words are often "detected" as a neighbour language): trust the first language.
fi

out=$(trans -b -s "$src" -t "$tgt" -- "$text" 2>/dev/null)
[ -z "$out" ] && exit 2

printf '%s\t%s\t%s\n' "$src" "$tgt" "$out"
