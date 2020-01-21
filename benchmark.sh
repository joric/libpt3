#!/bin/sh

for f in shiru zxssk
do
a=pt3_reader
gcc -O3 $a\_$f.c -o $a\_$f
./$a\_$f > data\_$f.h
done

for f in ayemu shiru joric
do
b=ay_render
gcc -O3 $b\_$f.c -o $b\_$f
./$b\_$f
done
