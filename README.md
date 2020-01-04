# libpt3

## Benchmarks

Results so far (use `benchmark.cmd 2>results.txt`):

```
shiru, pt3 reader, is_ts: 0, frames: 5376, loop: 384
0.011000 seconds
zxssk, pt3 reader, is_ts: 0, frames: 5376, loop: 0
0.011000 seconds
shiru, ay render, frames: 5376, writing wav...
0.173000 seconds
ayemul, ay render, frames: 5376, writing wav...
0.277000 seconds
```

## Notes

* AY register data 100% matches, that's a plus.
* Shiru player is far from accurate but it's 30% faster (!)

## TODO

* move mixer code out of the ChipTacts_per_outcount loop
* optimize for mono output (don't need mixer there at all)

## References

* https://github.com/joric/ts80player
