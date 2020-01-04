#!/usr/bin/env python3

import sys

if len(sys.argv)!=2:
    print("script.py [infile]")
    exit(0)

lines = open(sys.argv[1], 'r').read().splitlines()

print("const unsigned char frame_data[][14] = {")

for line in lines:
    v = line.split()
    print("\t{%s}," % ",".join(v))

print("};")


