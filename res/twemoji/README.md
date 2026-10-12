# Twemoji Mozilla

`Twemoji.Mozilla.ttf` is the colour emoji font of [mozilla/twemoji-colr](https://github.com/mozilla/twemoji-colr), release v0.7.0, unchanged. Each emoji is a stack of outline layers with a palette colour (the COLR/CPAL tables), so a program that reads outlines can draw it as vector art. `example/svg` does: pass the font as a fallback,

    microtex-svg --fallback res/twemoji/Twemoji.Mozilla.ttf "\text{🥰 💚}" out.svg

The engine itself does not use it; only the example, and only to draw emoji. Sequences that need shaping (a family, a flag) are drawn as their parts.

## Licence

The emoji art is [Twemoji](https://github.com/twitter/twemoji), Copyright Twitter, Inc and other contributors, used under the [CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/) licence; the font is built with Mozilla's code, Apache-2.0. [LICENSE.md](LICENSE.md) is the licence file of the font's repository.

The SVG files of [example/samples](../../example/samples/README.md) that show emoji carry this art, so the same credit applies to them.
