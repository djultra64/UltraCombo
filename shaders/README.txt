Shader presets for the Filter / Shader option, in libretro's GLSL format.

Bundled here, from https://github.com/libretro/glsl-shaders (licence headers
kept in each file):
  crt/crt-lottes*        public domain (Timothy Lottes)
  crt/crt-hyllian*       MIT (Hyllian)
  crt/crt-nobody         MIT
  crt/crt-1tap, 2tap     public domain
  scalefx/               MIT (Sp00kyFox)
  xbr/                   MIT (Hyllian)

`make shaders-extra` installs more into shaders/extra/: xBRZ, crt-guest,
crt-royale, crt-geom, crt-easymode, zfast-crt and crt-aperture. Those are
GPL-licensed; they are separate files read at run time, and keep their
licence headers. Any other libretro .glslp preset works too: put it (with
the files it uses) anywhere under this folder.
