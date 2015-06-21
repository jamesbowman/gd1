#include <SPI.h>
#include <GD.h>

#include "chopper.h"
#include "splitscreen.h"

class Controller {
  public:
    void begin() {
      pinMode(3, INPUT);
      digitalWrite(3, HIGH);
    }
    byte read() {
      return (!digitalRead(3));
    }
};

static Controller Control;
int atxy(int x, int y)
{
  return (y << 6) + x;
}

// ----------------------------------------------------------------------
// 7-segment display

// copy a (w,h) rectangle from the source image (x,y) into picture RAM
static void rect(unsigned int dst, byte x, byte y, byte w, byte h)
{
  prog_uchar *src = desert_pic + (16 * y) + x;
  while (h--) {
    GD.copy(dst, src, w);
    dst += 64;
    src += 16;
  }
}

static void digit(uint16_t dst, byte d)
{
  byte sx = 3 * (d % 5);
  byte sy = (d < 5) ? 32 : 36;
  rect(dst, sx, sy, 3, 4);
}


// ----------------------------------------------------------------------

static void map(uint16_t dst, uint16_t src)
{
  src &= 2047;
  int y = pgm_read_byte_near(roof + src);
  byte b;
  b = 0;
  while (y > -16) {
    draw_blocks(dst, y, b++, 4);
    y -= 16;
  }
  y = 256 - pgm_read_byte_near(foot + src);
  b = 0;
  while (y < 256) {
    draw_blocks(dst, y, b++, 0);
    y += 16;
  }
}

static void waitpress()
{
  delay(200);
  while (!Control.read())
    ;
  delay(200);
}

void setup()
{
  pinMode(6, INPUT);
  Control.begin();

  GD.begin();
  Serial.begin(1000000); // JCB

  GD.copy(PALETTE16A, chopper_palette, sizeof(chopper_palette));
  GD.copy(PALETTE16B, blocks_palette, sizeof(blocks_palette));
  GD.uncompress(RAM_SPRIMG, chopper_compressed);

  GD.uncompress(RAM_CHR, desert_chr_compressed);
  GD.uncompress(RAM_PAL, desert_pal_compressed);

  {
    prog_uchar *src = desert_pic;
    for (byte y = 0; y < 32; y++) {
      for (byte x = 0; x < 64; x += 16)
        GD.copy(atxy(x, y), src, 16);
      src += 16;
    }
  }
  byte blank = pgm_read_byte_near(desert_pic + (16 * 32));
  GD.fill(atxy(0,32), blank, 8 * 64);

  GD.microcode(splitscreen_code, sizeof(splitscreen_code));
  GD.wr16(COMM+0, 0);     // far SCROLL_X
  GD.wr16(COMM+2, 0);
  GD.wr16(COMM+4, 120);   // split at line 120
  GD.wr16(COMM+6, 0);     // near SCROLL_X
  GD.wr16(COMM+8, 0);
  GD.wr16(COMM+10, 256);  // turn sprites off at line 256
  GD.wr16(COMM+12, 0);
  GD.wr16(COMM+14, 0x8000 | 0);
}

void loop()
{
  uint16_t f = 0 << 4;
  int y = 100 << 4;
  int yv = 0;
  byte dying = 0;
  char msg[6];

  while (dying < (3 * 72) || !Control.read()) {
    byte frame = (f & 1);
    if (Control.read())
      yv -= 3;
    yv++;   // gravity
    if (!dying)
      y += yv;

    int x;
    if (!dying)
      x = f;

    int yp = y >> 4;
    GD.__wstartspr((frame ? 256 : 0));
    {
      int xm = (x >> 4);
      int xb = -(x & 15);
      for (int i = 0; i < 26; i++) {
        map(xb + (i << 4), xm + i);
      }
      sprintf(msg, "%04d", xm);
    }
    while (GD.spr != 248)
      GD.xhide();
    GD.__end();

    GD.__wstartspr((frame ? 256 : 0) + 248);
    if (!dying) {
      byte bf = min(max(0, yv >> 2), 3);
      draw_chopper(100, y >> 4, (2 * bf) + ((f >> 2) & 1), 0);
      // if ((yp < 16) || (268 < yp))
      //  dying = 1;
    } else {
      draw_dead(100, yp, (~(f >> 2) & 3), 0);
      byte ef = dying >> 3;
      if (ef < 8)
        draw_explode(100, yp, ef, 0);
      dying = min((3 * 72), dying + 1);
    }
    while (GD.spr != 8)
      GD.xhide();
    GD.__end();

    GD.waitvblank();
    if (!dying && f > 0) {
      GD.__start(COLLISION + 248);
      for (byte i = 0; i < 4; i++) {
        byte c = SPI.transfer(0);   // c is the colliding sprite number
        if (c != 0xff) {
          dying = 1;
        }
      }
      GD.__end();
    }
    GD.wr16(COMM+0, x >> 2);  // far SCROLL_X
    GD.wr16(COMM+6, x >> 1);  // near SCROLL_X
    GD.wr(SPR_PAGE, frame);

    if (dying) {
      byte v = max(0, 255 - dying * 6);
      GD.voice(0, 0, 220, v, v);
      GD.voice(1, 1, 220/8, v/2, v/2);
    } else {
      GD.voice(0, 0, 6 * 4000, 190, 0);
      GD.voice(0, 1, (f & 7) << 4,    0,   190);
    }

    digit(atxy(19, 32), msg[0] - '0');
    digit(atxy(22, 32), msg[1] - '0');
    digit(atxy(25, 32), msg[2] - '0');
    digit(atxy(28, 32), msg[3] - '0');
    f++;
  }
}
