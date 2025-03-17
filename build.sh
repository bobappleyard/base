#!/bin/sh

mkdir ./build

for f in src/*.c; do
    gcc -Wall -Werror -g -O3 -c -o ./build/`expr $f : 'src/\(.*\).c'`.o $f
done

gcc -static -o ./runtime ./build/*.o
