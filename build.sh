#!/bin/sh

cflags="-Wall -Wextra -pedantic -std=c99 -O3 -march=native"
ldflags="-lX11 -lXrandr -lm"

cc $cflags sct.c -o sct $ldflags
