
#include <SPI.h>
#include <GD.h>

#include "cp437.h"

static int atxy(int x, int y)
{
  return (y << 7) + x;
}

static void drawstr(uint16_t addr, const char *s)
{
  while (*s) {
    uint16_t w = pgm_read_word(cp437_pic + 2 * *s);
    GD.wr(addr, lowByte(w));
    GD.wr(addr + 64, highByte(w));
    s++, addr++;
  }
}

void setup()
{
  GD.begin();
  GD.uncompress(RAM_CHR, cp437_chr);
  GD.uncompress(RAM_PAL, cp437_pal);
  drawstr(atxy(0, 0), "Hello");
  drawstr(atxy(10, 2), "This is the cp437 font");
  for (byte i = 0; i < 14; i++) {
    drawstr(atxy(i, 4 + i), " *Gameduino* ");
  }
}

void loop()
{
}
