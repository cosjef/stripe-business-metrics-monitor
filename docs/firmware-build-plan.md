# Firmware Build Plan

State of the ESP32-C6 build, against
[stripe-revenue-display-spec.md](stripe-revenue-display-spec.md). For how the
device came to be on Arduino rather than ESP-IDF, and the hardware facts that
cost the most to establish, see [C6-HANDOFF.md](C6-HANDOFF.md).

The original ESP32-S3 plan this replaced is in git history
(`git log -- docs/firmware-build-plan.md`). Its stages are complete on a
device the repository no longer contains, and mixing them with this one made
both harder to read.

---

## Done

**Display and input.** CO5300 over QSPI via Arduino_GFX, LVGL bound to it,
rotation 3 (verified with a corner-marked test pattern, not by eye). Two side
buttons and the CST9217 touch panel both move the deck; a tap on the left
third goes back, the rest advances.

**Provisioning.** Open `Setup-XXXX` AP with a captive portal, two phases:
WiFi first, then the Stripe key — validated against the live API before it is
stored, so a bad key fails while the owner is still holding their phone.
Credentials go to NVS under the same keys the ESP-IDF build used.

**Fetch.** HTTPS to api.stripe.com with a pinned CA (DigiCert Assured ID Root
G2, the self-signed root rather than the cross-signed copy the server sends).
Subscriptions and invoices are parsed by streaming scanners that never hold
the response. Polls every five minutes.

**The deck.** Eight screens, five seconds each, hiding themselves when they
have nothing to say: MRR, new paid, paid subs, cancelled, ARR, ARPU, net 30-day,
failed payments.

**Honesty.** Cached values render at boot with their real age, so a stale
cache lands on the stale screen rather than presenting old figures as current.
A trend needs seven daily samples; an ARPU comparison needs six customers on
each side. Battery sensing via the AXP2101, with the low and critical screens.
WiFi reconnects with backoff, unbounded once the credentials have been proven.

---

## Known limitations

These are real and current. The first is a defect for anyone outside US
Eastern time.

- [ ] **Timezone is hardcoded.** `DEVICE_TZ` in `firmware-c6/src/main.cpp` is
      `EST5EDT,M3.2.0/2,M11.1.0/2`. It decides when the daily history rolls
      over, so anywhere else the day boundary lands at the wrong hour. Spec
      §9.1 step 5 wants this collected during setup, along with currency and
      which screens rotate.

- [ ] **NVS is unencrypted.** A deliberate trade: the stored key is read-only
      and restricted, so it leaks a subscriber count rather than the ability
      to move money. Encrypting it needs flash encryption with an
      eFuse-burned key, which is irreversible and complicates development
      flashing.

- [ ] **Provisioning is unencrypted.** The setup AP is open and the key form
      is served over plain HTTP, so for the few minutes of first-run setup
      anyone in radio range can capture the WiFi password and the Stripe key.
      Deliberate -- a WPA2 AP needs a passphrase the owner cannot know in
      advance, and HTTPS on an IP address produces a certificate warning worse
      than the exposure -- but it is the first thing to revisit if this ever
      ships to someone other than the person who built it.

- [ ] **No live MRR on the setup screen.** Spec §9.1 step 4 wants the number
      shown before setup completes — the moment that convinces a skeptic. The
      key is validated against the live API, so the figure is available; it is
      simply not displayed. Only ever visible during first-run setup.

- [ ] **Background colour is unverified on this panel.** `COLOR_BG` is
      `#000000`, inherited from an IPS panel where `0x04`–`0x30` collapsed to
      the same grey. This is an emissive display where a black pixel is off,
      so the spec's `#121211` may well be viable. Measure before changing it.

---

## Deliberately not built

Each of these was investigated and declined; the reasoning is in
[C6-HANDOFF.md](C6-HANDOFF.md) so it is not re-proposed.

- **Tap-to-advance via the IMU.** Built for the S3 and removed: corrupt I2C
  reads decode as large accelerations, and every filter that removed the false
  triggers also rejected real taps — 8 phantom advances in 60 seconds sitting
  untouched. `tapdetect.c` and `tapstatus.c` keep their tests and stay
  unwired.
- **The MRR sparkline.** `sparkline.c` is tested and unused.
- **Today's deltas from the events feed.** Measured on a live account: most
  days it would read zero, and it costs a third API call.
- **Trials and conversion screens.** Correctly hidden rather than missing —
  the rotation rules drop them when there is nothing to show.
