#include <SPI.h>
#include <GD.h>

#include "spectrum.h"
#include "spectrum_data.h"

void setup()
{
  GD.begin();
  GD.microcode(spectrum_code, sizeof(spectrum_code));
  GD.uncompress(0x7000, spectrum_tables);

  // fill screen with TRANSPARENT
  GD.fill(RAM_PIC, 0xff, 64 * 64);
  GD.wr16(RAM_PAL + 8 * 255, TRANSPARENT);
  GD.wr16(BG_COLOR, RGB(128, 0, 0));  // dark red border

  // paint the 256x192 window as alternating lines of
  // chars 0-31 and 32-63
  for (byte y = 0; y < 24; y += 2) {
    for (byte x = 0; x < 32; x++) {
      GD.wr(RAM_PIC + 64 * (y + 6) + (9 + x), x);
      GD.wr(RAM_PIC + 64 * (y + 7) + (9 + x), 32 + x);
    }
  }

  // Slow things down for a proper loading-from-tape look
  SPI.setClockDivider(SPI_CLOCK_DIV128);
}

static void pause()
{
  delay(3000);
  GD.fill(0x4000, 0x00, 32 * 192);
  GD.fill(0x5800, 0x07, 3 * 256);
}

void loop()
{
  GD.uncompress(0x4000, screen_mm1);  pause();
  GD.uncompress(0x4000, screen_mm2);  pause();
  GD.uncompress(0x4000, screen_aa0);  pause();
  GD.uncompress(0x4000, screen_aa1);  pause();
  GD.uncompress(0x4000, screen_jp0);  pause();
  GD.uncompress(0x4000, screen_jp1);  pause();
  GD.uncompress(0x4000, screen_kl0);  pause();
  GD.uncompress(0x4000, screen_kl1);  pause();
}
