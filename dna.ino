#include <stdlib.h>
#include <SPI.h>
#include <GD.h>

static uint16_t SWAP_RB(uint16_t color) // Swap red and blue channel
{
  byte r = (color >> 10) & 31;
  byte g = (color >> 5) & 31;
  byte b = color & 31;
  return (color & 0x8000) | (b << 10) | (g << 5) | (r);
}

static uint16_t SWAP_RG(uint16_t color) // Swap red and blue channel
{
  byte r = (color >> 10) & 31;
  byte g = (color >> 5) & 31;
  byte b = color & 31;
  return (color & 0x8000) | (g << 10) | (r << 5) | (b);
}

#include "dna.h"

////////////////////////////////////////////////////////////////////////////////
//                                  3D Projection
////////////////////////////////////////////////////////////////////////////////

static float mats[2][9];

static float mat[9];

// Taken from glRotate()
static void rotation(float phi)
{
  float x = 0.57735026918962573;
  float y = 0.57735026918962573;
  float z = 0.57735026918962573;

  float s = sin(phi);
  float c = cos(phi);

  mat[0] = x*x*(1-c)+c;
  mat[1] = x*y*(1-c)-z*s;
  mat[2] = x*z*(1-c)+y*s;

  mat[3] = y*x*(1-c)+z*s;
  mat[4] = y*y*(1-c)+c;
  mat[5] = y*z*(1-c)-x*s;

  mat[6] = x*z*(1-c)-y*s;
  mat[7] = y*z*(1-c)+x*s;
  mat[8] = z*z*(1-c)+c;
}

#ifdef MAPLE_IDE
#define NVERTICES 250
#else
#define NVERTICES 220   // Arduino does not have enough RAM for all 250 
#endif

struct screenpt {
  short x, y, z;
};
static struct screenpt projected[NVERTICES];

void project(float distance)
{
  byte vx;
  prog_char *pm = cloud; 
  prog_char *pm_e = cloud + (NVERTICES*3);
  struct screenpt *dst = projected;
  signed char x, y, z;

  while (pm < pm_e) {
    x = pgm_read_byte_near(pm++);
    y = pgm_read_byte_near(pm++);
    z = pgm_read_byte_near(pm++);
    float xx = x * mat[0] + y * mat[3] + z * mat[6];
    float yy = x * mat[1] + y * mat[4] + z * mat[7];
    float zz = x * mat[2] + y * mat[5] + z * mat[8] + distance;
    int scale = 200;
    float q = scale / (250 + zz);
    dst->x = (511 & int(200 + xx * q)) | (((x^y^z) & 3) << 14);
    dst->y = int(150 + yy * q);
    dst->z = int(zz * 100);
    dst++;
  }
}

// point depth comparison, for depth sort
int ptcmp(const void *va, const void *vb)
{
  struct screenpt *a = (struct screenpt *)va;
  struct screenpt *b = (struct screenpt *)vb;

  if (a->z < b->z)
    return 1;
  else if (a->z > b->z)
    return -1;
  else
    return 0;
}

// load a sprite at (x,y).  z is distance 0-63, color is 0-3
static void render_sphere(int x, int y, byte z, byte color)
{
  static byte pals[4][2] = {
    { 4,6 },
    { 5,7 },
    { 0,1 },
    { 2,3 }};

  int ox = x - 8;
  int oy = y - 8;
  byte palette = pals[color][z & 1];
  byte image = (z >> 1);

  SPI.transfer(lowByte(ox));
  SPI.transfer((palette << 4) | (highByte(ox) & 1));
  SPI.transfer(lowByte(oy));
  SPI.transfer((image << 1) | (highByte(oy) & 1));
}

void draw(float distance)
{
  project(distance);
  qsort(projected, NVERTICES, sizeof(struct screenpt), ptcmp);

  static byte flip;
  GD.__wstartspr(flip ? 256 : 0);
  for (int i = 0; i < NVERTICES; i++) {
    byte color = (projected[i].x >> 14) & 3;
    short x = projected[i].x & 511;
    short y = projected[i].y;
    int z = max(0, min(63, int(32 + projected[i].z / 500)));
    render_sphere(x, y, z, color);
  }
  GD.__end();
  GD.wr(SPR_PAGE, flip);
  // if (flip == 0) rawdump(RAM_SPR, 1024);  // JCB
  flip = !flip;
}

static float phi;    // Current rotation angle

// Draw one frame of ship
void cycle(float distance)
{
  rotation(phi);
  phi += 0.02;
  draw(distance);

  // if (0)  // JCB
  { // report frame rate in top-right
    static byte every;
    if (++every == 4) {
      static long tprev;
      long t = micros();
      every = 0;

      char msg[30];
      int fps10 = int(4 * 10000000UL / (t - tprev));
      sprintf(msg, "%3d.%d fps  ", fps10 / 10, fps10 % 10);
      GD.putstr(41, 0, msg);
      tprev = t;
    }
  }
}

static uint16_t rdpal(byte i)
{
  return pgm_read_word_near(sphere_pal + (i << 1));
}

void setup()
{
  GD.begin();
  GD.ascii();
  Serial.begin(1000000);  // JCB

  for (byte y = 0; y < 38; y++) {
    prog_uchar *src = ramp_pic + y * 4;
    for (byte x = 0; x < 50; x++)
      GD.wr(RAM_PIC + y * 64 + x, pgm_read_byte(src + random(4)));
  }
  GD.copy(RAM_CHR + 128 * 16, ramp_chr, sizeof(ramp_chr));
  GD.copy(RAM_PAL + 128 * 8, ramp_pal, sizeof(ramp_pal));

  GD.copy(PALETTE16A, sphere_pal, sizeof(sphere_pal));
  for (byte i = 0; i < 16; i++) {
    GD.wr16(PALETTE16B + 2 * i, SWAP_RB(rdpal(i)));
  }

  for (int i = 0; i < 256; i++) {
    // palette 0 decodes low nibble, hence (i & 15)
    GD.wr16(RAM_SPRPAL + (i << 1), SWAP_RG(rdpal(i & 15)));
    // palette 1 decodes nigh nibble, hence (i >> 4)
    GD.wr16(RAM_SPRPAL + 512 + (i << 1), SWAP_RG(rdpal(i >> 4)));

    // palette 0 decodes low nibble, hence (i & 15)
    GD.wr16(RAM_SPRPAL + 1024 + (i << 1), SWAP_RB(SWAP_RG(rdpal(i & 15))));
    // palette 1 decodes nigh nibble, hence (i >> 4)
    GD.wr16(RAM_SPRPAL + 1024 + 512 + (i << 1), SWAP_RB(SWAP_RG(rdpal(i >> 4))));
  }

  GD.copy(RAM_SPRIMG, sphere_img, sizeof(sphere_img));
  // rawdump(0, 32768);  // JCB

#ifdef MAPLE_IDE
  GD.putstr(0, 0, "DNA demo: Gameduino with Maple");
#else
  GD.putstr(0, 0, "DNA demo: Gameduino with Arduino");
#endif
}

void loop()
{
  cycle(0.0);
}
