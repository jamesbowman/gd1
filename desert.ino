#include <SPI.h>
#include <GD.h>

#include "desert.h"
#include "bgstripes.h"

void setup()
{
  GD.begin();

  // Load sprite graphics
  GD.copy(PALETTE16A, palette16a, sizeof(palette16a));
  GD.copy(PALETTE16B, palette16b, sizeof(palette16b));
  GD.uncompress(RAM_SPRIMG, sprimg_cc);

  // Load background tile graphics
  GD.uncompress(RAM_CHR, dchr);
  GD.uncompress(RAM_PAL, dpal);

  // Load 'sunset' as a coprocessor program and the 'desert' color ramp
  GD.microcode(bgstripes_code, sizeof(bgstripes_code));
  GD.copy(0x3e80, desert, sizeof(desert));
}

void loop()
{
  int r = 0;
  int ypos;
  int delta = 512;
  int speedup = 0;
  byte dirty = 32;    // redraw whole screen first time through

  for (ypos = 2048 - 299; ypos > 0; ypos -= (delta >> 9)) {
    delta++;

    int yd = ypos >> 4;

    // Draw background tiles, each tile is 2x2 chars
    byte x, y;
    for (y = 0; y < dirty; y++) {
      uint16_t ty = (((y + yd) & 31) << 7);
      flash_uint8_t *lp = level + ((yd + y) << 5);

      for (x = 0; x < 25; x++) {
        byte t = pgm_read_byte_near(lp++);
        if (t == 4)
          t = 0;
        flash_uint8_t *pt = tiles + (t << 2);
        int da = (x << 1) + ty;
        GD.copy(da, pt, 2);
        GD.copy(da + 64, pt + 2, 2);
      }
    }
    dirty = 1;

    // Sprites: first draw the player sprite
    GD.__wstartspr(r << 8);
    int jy = (2048-44) - ypos;
    if (jy < 400)
      draw_walk(100, jy, 0, 0);

    // draw all the fruit, in every tile '4'
    for (y = 0; y < 24; y++) {
      flash_uint8_t *lp = level + ((yd + y) << 5);
      for (x = 0; x < 25; x++) {
        byte t = pgm_read_byte_near(lp++);
        if (t == 4)
          draw_fruit(16 * x + 8, 16 * y - (ypos & 15) + 8, ((x + y + yd) % 6), 0);
      }
    }

    // blank unused sprite slots
    while (GD.spr)
      GD.xhide();
    GD.__end();

    GD.waitvblank();
    GD.wr16(SCROLL_Y, ypos);        // main background scroll
    GD.wr(COMM+0, 236 - yd);        // move the 'sky' down
    GD.wr(SPR_PAGE, r & 1);         // show sprites
    r ^= 1;
  }
  delay(2000);
}
