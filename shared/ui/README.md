# Drawn-paper UI kit

The look shared by the Potluck Audio plugins, after Tilly: paper with grain, colour washes with rough
edges, SF Pro Rounded type, an 8 fps "boil", a front and a back that flip.

For now the only copy lives inside Puppy Chow's page, `plugins/puppy-chow/src/ui/index.html`
(the "drawing helpers" and "theme" sections: paper and grain, `rough`, `washPath`, `wash`,
`sticker`, `light`, the light/dark paper palette) together with the C++ side in
`plugins/puppy-chow/src/PluginEditor.cpp` (serving the page to the web view and passing
parameters back and forth).

It moves here when the second plugin starts, so the kit's shape is set by two real
interfaces rather than guessed from one. The plan at that point:

- `ui/drawn-paper.js`: the drawing helpers as one module that takes a canvas and a palette.
- `ui/WebViewEditor.h`: the JUCE web-view editor base (page serving, parameter bridge,
  `ready`/`set`/`begin`/`end` events, the saved `ui_dark` look setting).
- Each plugin's `index.html` keeps only its own layout and controls.
