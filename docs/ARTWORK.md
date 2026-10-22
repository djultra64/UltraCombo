# Optional widescreen artwork

The presentation draws lightning artwork behind the centered game viewport,
then a narrow contact shadow with a soft falloff outside that rectangle. The
original game and menu overlay remain in the same opaque 4:3 viewport. Artwork
is never fed through the game shader or written into emulated state.

Graphics > Background art toggles this presentation (`[video] artwork=on/off`
in each game's ini). The default is on. Missing artwork falls back to black
borders. Fit, integer and stretch keep their existing game geometry; stretch
covers the artwork completely. Portrait windows and integer-scale top/bottom
margins use the same composition. The background covers the drawable without
changing its aspect ratio; excess artwork is cropped centrally.

Both OpenGL and the SDL renderer use the same host-only composition. The
background is uploaded only when drawable size or scaling changes, and is
cached between frames. At 1920x1080 the shadow has an approximately 2-pixel
contact edge and a soft spread of about 24 pixels. Its size follows the game
viewport height, including HiDPI output.

`artwork/lightning.png` is supplied unchanged by djultra64 from our earlier
native-port work. See `artwork/README.md` for provenance and reuse terms.
The visual reference was used only to study the shadow; none of its imagery
is included. No ROM, CHD, gameplay capture or translated game code is needed
by the tests or included with this contribution.

## Validation

`make test` includes synthetic geometry and composition checks without a display
server. `make test-video-artwork` exercises both renderers under an isolated
X server; it requires a working OpenGL driver (Mesa software rendering is fine).

### Verified on Linux, 2026-10-22

- Full `ultracombo-dist` build: PASS.
- `make test`: state-file validation (131 checks), artwork geometry/shadow,
  and legacy/default/off/on configuration round-trip: PASS.
- `make test-video-artwork`: OpenGL and SDL fallback: PASS. Each compares every
  game/overlay pixel with artwork on/off across all four built-in filters,
  fit/integer/stretch, portrait, resize and 3840x2160 output. OpenGL also checks
  the bundled CRT Lottes multipass preset. Both load the actual bundled PNG and
  compare sampled border pixels to the shared compositor, including orientation.
- Independent read-only review: no remaining actionable findings.
- Local KI1 interpreter smoke: 2,400 frames reached character selection; the
  resulting game frame was displayed through the real OpenGL presentation for
  private visual inspection. No gameplay capture is included in this PR.
- Windows runtime and real HiDPI hardware: NOT RUN. Package copy paths were
  updated for Windows, Linux and inherited AppImage contents; complete release
  packages were not built in this environment.
