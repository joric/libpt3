#!/bin/sh

for f in shiru zxssk; do
gcc -O3 pt3_reader_$f.c -o pt3_reader_$f
./pt3_reader_$f > data_$f.h
done

diff -u data_shiru.h data_zxssk.h

for f in ayemu shiru; do
gcc -O3 ay_render_$f.c -o ay_render_$f
./ay_render_$f
done

