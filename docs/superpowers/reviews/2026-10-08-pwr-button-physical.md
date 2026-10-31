# PWR button — physical observation — 2026-10-08

## Outcome

**A LONG PRESS ON PWR POWERS `torget-216-02` OFF, ALSO WITH USB CONNECTED.**
The owner held the board's PWR button with the panel running
`v1.1.0-50-g8acb202`, a cell fitted and the USB-C cable plugged into a hub on
the Mac. The panel went dark, and on the Mac the board's USB device
(`USB JTAG_serial debug unit`, serial `44:BD:8D:60:DC:F0`) disappeared from
`ioreg` and `/dev/cu.usbmodem*`. The rails were cut, not only the display.

## Power-on, later the same evening

**A long press on PWR powers the panel on again.** The owner reports that a
short press does nothing; the press that starts the panel is about as long
as the one that powers it off. The vendor's "short press to power on" does
not match this unit. The panel came back on the Mac's USB afterwards.

## What this does not cover

- The owner's planned run of TID without a network (away from Wi-Fi
  coverage) has not happened yet; this power-on was at home with Wi-Fi.
- The reset reason the firmware logs after such a start (expected: a true
  power-on, so `tid från RTC`) was not read: a console cannot be attached
  before the port exists, and opening it resets the panel.
- The press duration was not timed. The AXP2101's default is in the seconds;
  "long" here means the owner's hold until the panel went dark.
- Nothing in the firmware handles the button: the PMIC acts on its own
  PWRON input. Whether the panel can be configured to ignore or shorten it
  was not looked at.
- One unit; `torget-home-01` has no cell and was not tried.
