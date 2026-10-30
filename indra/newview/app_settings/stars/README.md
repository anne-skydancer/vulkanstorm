# Night sky stars

`stars.bin` holds the 8,920 stars of visual magnitude 6.5 or brighter (the
stars visible to the naked eye under a dark sky): right ascension,
declination, magnitude and B-V colour index. The viewer draws its real night
sky from it (`indra/newview/llnightsky.cpp`, `deferred/skyF.glsl`).

It is derived from the **HYG database v4.4** by David Nash (astronexus),
https://codeberg.org/astronexus/hyg, licensed under the
[Creative Commons Attribution-ShareAlike 4.0 International License](https://creativecommons.org/licenses/by-sa/4.0/)
(`LICENSE-HYG.md`). `stars.bin` is distributed under the same licence, not
the viewer's. Changes: only the stars of magnitude 6.5 or brighter are kept,
without the Sun, sorted by magnitude, and their position, magnitude and
colour index are rounded to fixed-point values.

To rebuild it from a newer release of the database:

```
python scripts/generate-star-catalog.py hyg_v44.csv.gz indra/newview/app_settings/stars/stars.bin
```
