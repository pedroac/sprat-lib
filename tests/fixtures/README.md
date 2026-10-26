# Test fixtures

`multipack.sprat` is real `spratconvert` output (sprat-cli v0.21.0), generated
from the inputs in `sources/`:

```sh
spratconvert --transform raw \
  --animations sources/multipack.animations.txt \
  --markers sources/multipack.markers.txt \
  -a 'atlas_%d.png' --normal-atlas 'n_%d.png' \
  < sources/multipack.layout.txt > multipack.sprat
```

It covers multipack, scale, extrude, counter-clockwise rotation, trims, a
nine-patch slice with a non-default fill mode, a deduplicated sprite alias,
per-sprite and global pivots, markers, and an animation alias with a flip.
Regenerate it when sprat-cli changes the raw transform.

`controls.sprat` is `spratconvert` output (sprat-cli v0.21.2) for sprites whose
names hold a bidirectional override (U+202E), the Arabic letter mark (U+061C), a
bidirectional isolate (U+2067) and a C1 control (U+009B). `sprat-dump` must
print them escaped:

```sh
spratconvert --transform raw -a atlas.png \
  < sources/controls.layout.txt > controls.sprat
```
