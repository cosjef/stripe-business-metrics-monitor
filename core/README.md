# core

The portable half of the firmware: everything that computes or draws, and
nothing that touches a panel, a radio or an SDK.

Both products build these files directly — `firmware-s3` through its
CMakeLists, `firmware-c6` through `build_src_filter` in platformio.ini.
Neither owns them, and neither reaches into the other.

```
core/
  src/       screen rendering, MRR maths, the streaming parsers, rotation
  include/   their headers, plus the layout constants for both panels
  fonts/     generated bitmap faces (8.6MB; regenerate with tools/gen_fonts.sh)
  test/      the host suite, 1,591 checks across 28 suites
  tools/     font generation and the advance-width dumper
```

## Why this is a peer rather than part of a product

It used to live in `firmware/main/`, and the C6 build reached into it with
`../../firmware/main/screens.c`. That made the S3 tree structurally
privileged: reorganising it broke a build for a different board that had not
changed. Moving the shared code out means both products depend on it and
neither depends on the other.

## The rule

Nothing in here may include `esp_*.h`, `<Arduino.h>`, `driver/*` or
`freertos/*`. If a file needs the hardware, it belongs to a product, not
here. That constraint is what lets the whole thing be tested on a laptop.

## Tests

```sh
cd core/test && make && for t in ./test_*; do [ -x "$t" ] && $t; done
```

They need LVGL, which arrives with the ESP-IDF component manager:
`cd firmware-s3 && idf.py reconfigure`.
