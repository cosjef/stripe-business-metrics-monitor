# core

The portable half of the firmware: everything that computes or draws, and
nothing that touches a panel, a radio or an SDK.

`firmware-c6` builds these files directly through `build_src_filter` in
platformio.ini, rather than vendoring a copy. The split is what keeps the
logic testable without hardware.

```
core/
  src/       screen rendering, MRR maths, the streaming parsers, rotation
  include/   their headers, plus the layout constants for both panels
  fonts/     generated bitmap faces (8.6MB; regenerate with tools/gen_fonts.sh)
  test/      the host suite, 1,520 checks across 25 suites
  tools/     font generation and the advance-width dumper
```

## Why this is separate from the firmware

It began inside the firmware tree, and the device build reached into it with
relative paths. Pulling it out is what makes the boundary enforceable: the
rule below can be checked, and the tests can run without an SDK.

The project previously carried a second board, and this split is what let both
share one implementation. That board is gone, but the separation earns its
keep regardless -- it is the reason 1,520 checks run on a laptop.

## The rule

Nothing in here may include `esp_*.h`, `<Arduino.h>`, `driver/*` or
`freertos/*`. If a file needs the hardware, it belongs to a product, not
here. That constraint is what lets the whole thing be tested on a laptop.

## Tests

```sh
cd core/test && make && for t in ./test_*; do [ -x "$t" ] && $t; done
```

They need LVGL, which arrives with the PlatformIO dependency:
`cd firmware-c6 && pio pkg install`.
