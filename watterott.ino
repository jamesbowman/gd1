#include <SPI.h>
#include <GD.h>

// ----------------------------------------------------------------------
//     qrand: quick random numbers
// ----------------------------------------------------------------------

static uint16_t lfsr = 1;

static void qrandSeed(int seed)
{
  if (seed) {
    lfsr = seed;
  } else {
    lfsr = 0x947;
  }
}

static byte qrand1()    // a random bit
{
  lfsr = (lfsr >> 1) ^ (-(lfsr & 1) & 0xb400);
  return lfsr & 1;
}

static byte qrand(byte n) // n random bits
{
  byte r = 0;
  while (n--)
    r = (r << 1) | qrand1();
  return r;
}
// ----------------------------------------------------------------------

#include "watterott_logo.h"
#include "bgstripes.h"

int focusx, focusy;

// JCB-PARTICLE-A
#define NSPR 200

struct spr {
  int x, y;
  signed char vx, vy;
} sprites[NSPR];

static void born(byte i)
{
  sprites[i].x = focusx + qrand(4);
  sprites[i].y = focusy -16 + qrand(5);
  sprites[i].vx = -8 + qrand(4);
  sprites[i].vy = -qrand(4) - 1;
}

static void kill(byte i)
{
  sprites[i].y = 309;
  sprites[i].vx = 0;
  sprites[i].vy = 0;
}
// JCB-PARTICLE-B

int atxy(int x, int y)
{
  return (y << 6) + x;
}

static byte sprcnt = 0;

void setup()
{
  GD.begin();
  Serial.begin(1000000); // JCB

  // Black-to-gray transition starting at line 150
  GD.microcode(bgstripes_code, sizeof(bgstripes_code));
  GD.wr16(COMM+0, 112);

  GD.uncompress(RAM_CHR, logobg_chr_compressed);

  GD.copy(PALETTE4A, spark_palette, sizeof(spark_palette));
  GD.uncompress(RAM_SPRIMG, wlogo_compressed);

}

void bgfade(byte t)
{
  for (byte i = 0; i < 64; i++) {
    byte l = ((t == 31) ? 8 : 0) + ((i * t) >> 4);
    GD.wr16(0x3e80 + 2 * i, RGB(l, l, l));
  }
}

static void fade(uint16_t addr)
{
  uint16_t col = GD.rd16(addr);
  if (col & (31 << 10)) col -= (1 << 10);
  if (col & (31 << 5))  col -= (1 << 5);
  if (col & 31)         col -= 1;
  GD.wr16(addr, col);
}

// Fade 16 palette entries starting at addr
static void fade16(uint16_t addr)
{
  uint16_t cols[16];
  GD.__start(addr);
  for (byte i = 0; i < 16; i++) {
    byte lo = SPI.transfer(0);
    byte hi = SPI.transfer(0);
    uint16_t col = (hi << 8) | lo;
    if (col & (31 << 10)) col -= (1 << 10);
    if (col & (31 << 5))  col -= (1 << 5);
    if (col & 31)         col -= 1;
    cols[i] = col;
  }
  GD.__end();

  GD.__wstart(addr);
  for (byte i = 0; i < 16; i++) {
    uint16_t col = cols[i];
    SPI.transfer(lowByte(col));
    SPI.transfer(highByte(col));
  }
  GD.__end();
}

#define FINALX 305

void loop()
{
  focusy = 138;
  sprcnt = 0;

  for (byte i = 0; i < NSPR; i++) {
    kill(i);
  }
  GD.__wstartspr(0);
  for (int i = 0; i < 512; i++)
    GD.xhide();
  GD.__end();
  GD.copy(PALETTE16A, wlogo_palette, sizeof(wlogo_palette));
  GD.uncompress(RAM_PAL, logobg_pal_compressed);

  byte jitter = 0;

  for (int t = 0; t < 8 * 72; t++) {
    byte frame = (t & 1);

    if (t < 32)
      bgfade(t);
    if (((8 * 72) - 32) < t) {
      bgfade(8 * 72 - t);
      for (uint16_t a = RAM_PAL; a < RAM_PAL + (256 * 4 * 2); a += 32)
        fade16(a);
      fade16(PALETTE16A);
    }

    byte sparking = (72 <= t) && (t <= (72 + FINALX));
    if (sparking) {
      focusx = t - 72;
      if (16 <= focusx && ((focusx & 15) == 0)) {
        GD.__wstartspr(sprcnt);
        draw_wlogo(focusx, 109, (focusx - 16) >> 4, 0);
        GD.__end();
        GD.__wstartspr(256 | sprcnt);
        draw_wlogo(focusx, 109, (focusx - 16) >> 4, 0);
        GD.__end();
        sprcnt += GD.spr;
      }
    }

#if 1
    GD.__wstartspr((frame ? 256 : 0) + 56);
    byte i;
    // JCB-SPARK-A
    struct spr *ps;
    for (i = 0, ps = &sprites[i]; i < NSPR; i++, ps++) {
      draw_spark(ps->x, ps->y, 0, (i & 7));
      ps->x += ps->vx;
      ps->y += ps->vy;
      ps->vy++;
      if ((ps->x < 0) || (ps->x > 400) || (ps->y > 310)) {
        if (sparking)
          born(i);
        else
          kill(i);
      }
    }
    // JCB-SPARK-B
    GD.__end();
#endif

    GD.waitvblank();
    GD.wr(SPR_PAGE, frame);
    // JCB-JITTER-A
    GD.wr16(SCROLL_X, random(-1, 2));
    GD.wr16(SCROLL_Y, random(-1, 2));
    flash_uint8_t *src = logobg_pic + (12 * 25 * jitter);
    for (byte y = 0; y < 12; y++) {
      GD.copy(atxy(24, 12 + y), src, 25);
      src += 25;
    }
    // JCB-JITTER-B
    jitter = (jitter == 4) ? 0 : (jitter + 1);

    // static int ss; GD.screenshot(ss++); // JCB
  }
  delay(700);
}
