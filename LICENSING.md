# Licensing

This repository uses two licenses, chosen by file type:

| What | License | File |
|---|---|---|
| Source code: `*.cpp`, `*.h`, `CMakeLists.txt`, `*.cmake`, scripts | MIT | [LICENSE](LICENSE) |
| Course text and images: `*.md` (all languages), `docs/images/`, `lessons/*/docs/images/` | Creative Commons Attribution 4.0 International (CC BY 4.0) | [LICENSE-docs](LICENSE-docs) |

Copyright (c) 2026 Tom Hsieh.

When you reuse the course text (for example in your own class notes or a translation), CC BY 4.0
requires attribution: credit "DawLab by Tom Hsieh", link to this repository and to the license, and
say whether you changed the material.

## Third-party software

Third-party code is **not** part of this repository and keeps its own license. The build fetches or
uses it from `third_party/`:

- **JUCE** (chapters 13 and 14 only). JUCE 9 modules are dual-licensed under the
  [AGPLv3](https://www.gnu.org/licenses/agpl-3.0.en.html) and the commercial
  [JUCE 9 End User Licence Agreement](https://juce.com/legal/juce-9-licence/). The MIT-licensed code
  in this repository may be combined with JUCE, but **if you distribute a plugin or application
  built with JUCE (for example the chapter 13 plugins or DawLab 8), the combined work must comply
  with the AGPLv3**, which in practice means publishing its complete source code under the AGPLv3,
  unless you hold a commercial JUCE licence.
- **VST3 SDK** (bundled inside JUCE): MIT License, Copyright (c) 2025 Steinberg Media Technologies
  GmbH. Using the "VST" name or logo is subject to Steinberg's usage guidelines.
- Chapters 00–12 and the DAW engine (`lessons/14-daw-8track/engine/`) use only the C++ standard
  library and are not affected by the JUCE licence.

This summary is informational, not legal advice; the license texts are authoritative.
