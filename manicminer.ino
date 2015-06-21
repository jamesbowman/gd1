#include <SPI.h>
#include <GD.h>

#define CHEAT_INVINCIBLE    0   // means Willy unaffected by nasties
#define START_LEVEL         0   // level to start on, 0-18
#define CHEAT_OPEN_PORTAL   0   // Portal always open

// Game has three controls: LEFT, RIGHT, JUMP.  You can map them
// to any pins by changing the definitions of PIN_L, PIN_R, PIN_J
// below.

static int ss;  // JCB
#if 1 // SPARKFUN_JOYSTICK
#define PIN_L  6
#define PIN_R  3
#define PIN_J  4
#else
#define PIN_L  A2
#define PIN_R  A3
#define PIN_J  A5
#endif

#define CONTROL_LEFT  1
#define CONTROL_RIGHT 2
#define CONTROL_JUMP  4

static byte setup_control()
{
  pinMode(PIN_L, INPUT);
  digitalWrite(PIN_L, HIGH);
  pinMode(PIN_R, INPUT);
  digitalWrite(PIN_R, HIGH);
  pinMode(PIN_J, INPUT);
  digitalWrite(PIN_J, HIGH);
}

static byte control()
{
  byte r = 0;
  if (!digitalRead(PIN_J))
    r |= CONTROL_JUMP;
  if (!digitalRead(PIN_L))
    r |= CONTROL_LEFT;
  if (!digitalRead(PIN_R))
    r |= CONTROL_RIGHT;
  return r;
}

#define BLACK   RGB(0,0,0)
#define RED     RGB(255,0,0)
#define YELLOW  RGB(255,255,0)
#define CYAN    RGB(0,255,255)
#define GREEN   RGB(0,255,0)
#define MAGENTA RGB(255,0,255)
#define WHITE   RGB(255,255,255)

#define CHR_BORDER    31    // hw char used for all of the border
#define CHR_AIR       128   // AIR line

                            // assigned sprite images and sprite numbers
#define IMG_ITEM      0     // items 0-4
#define IMG_PORTAL    5     // room exit
#define IMG_SWITCH1   6     // the switches for the Kong Beast levels
#define IMG_SWITCH2   7     
#define IMG_GUARD     8     // All guardians, 8 max
#define IMG_NASTY1    16    // Nasty blocks
#define IMG_NASTY2    17
#define IMG_WILLYC    56
#define IMG_WILLY     63

#define ELEM_AIR      0
#define ELEM_FLOOR    1
#define ELEM_CRUMBLE  2
#define ELEM_WALL     3
#define ELEM_CONVEYOR 4
#define ELEM_NASTY1   5
#define ELEM_NASTY2   6

#include "manicminer.h"
#include "spectrum.h"
#include "spectrum_data.h"    // title screen

#define COLOR(i)  (pgm_read_word_near(specpal + (2 * (i))))
#define BRIGHT(x) (((x) & 64) >> 3)
#define PAPER(a)  ((((a) >> 3) & 7) | BRIGHT(a))
#define INK(a)    (((a) & 7) | BRIGHT(a))

// screen coordinate - Spectrum 256x192 screen is centered in Gameduino screen
static uint16_t atxy(byte x, byte y)
{
  return RAM_PIC + 64 * (y + 6) + (x + 9);
}

// unpack 8 monochrome pixels into Gameduino character RAM
void unpack8(byte b)
{
  static byte stretch[16] = {
    0x00, 0x03, 0x0c, 0x0f,
    0x30, 0x33, 0x3c, 0x3f,
    0xc0, 0xc3, 0xcc, 0xcf,
    0xf0, 0xf3, 0xfc, 0xff
  };
  SPI.transfer(stretch[b >> 4]);
  SPI.transfer(stretch[b & 15]);
}

// unpack monochrome bitmap from flash to Gameduino character RAM at dst
void unpack8xn(uint16_t dst, flash_uint8_t *src, uint16_t size)
{
  GD.__wstart(dst);
  while (size--)
    unpack8(pgm_read_byte_near(src++));
  GD.__end();
}

// Load 8x8 1-bit sprite from src into slot (0-63)
// zero and one are the two colors

void send8(byte b, byte zero, byte one)
{
    for (byte j = 8; j; j--) {
      SPI.transfer((b & 0x80) ? one : zero);
      b <<= 1;
    }
}

// load an 8x8 into a hw 256-color sprite, padding with transparent (color 255)
void loadspr8(byte slot, flash_uint8_t *src, byte zero, byte one)
{
  GD.__wstart(RAM_SPRIMG + (slot << 8));
  for (byte i = 8; i; i--) {
    send8(pgm_read_byte_near(src++), zero, one);
    send8(0, 255, 255);
  }
  for (byte i = 128; i; i--)
    SPI.transfer(255);
  GD.__end();
}

// load an 16x16 into a hw 256-color sprite
void loadspr16(byte slot, flash_uint8_t *src, byte zero, byte one)
{
  GD.__wstart(RAM_SPRIMG + (slot << 8));
  for (byte i = 32; i; i--)
    send8(pgm_read_byte_near(src++), zero, one);
  GD.__end();
}


static void sprite(byte spr, byte x, byte y, byte img, byte rot = 0)
{
  GD.sprite(spr, x + 9*8, y + 6*8, img, 0, rot);
}

static void hide(byte spr)
{
  GD.sprite(spr, 400, 400, 0, 0, 0);
}

struct guardian {
  byte a;
  byte x, y;
  signed char d;
  byte x0, x1;
  byte f;
} guards[8];

#define MAXRAY 40

struct state_t {
  byte level;
  byte lives;
  uint32_t score;
  uint32_t hiscore;

  byte bidir;
  byte air;
  byte conveyordir;
  byte portalattr;
  byte nitems;
  byte wx, wy;    // Willy x,y
  byte wd, wf;    // Willy dir and frame
  byte lastdx;    // Willy last x movement
  byte convey;    // Willy caught on conveyor
  byte jumping;
  signed char wyv;
  byte conveyor[2];
  flash_uint8_t *guardian;
  uint16_t prevray[MAXRAY];
  byte switch1, switch2;
} state;

// All this is taken from
// http://jswremakes.emuunlim.com/Mmt/Manic%20Miner%20Room%20Format.htm

static void plot_air()
{
  // Starting at CHR_AIR+4, draw a line of n/8 solid, n%8, then 27-(n/8)
  uint16_t addr = RAM_CHR + (16 * (CHR_AIR + 4)); // line 2 of (CHR_AIR+4)
  for (byte i = 0; i < 28; i++) {
    byte v;
    if (i < (state.air >> 3))
      v = 8;
    else if (i == (state.air >> 3))
      v = state.air & 7;
    else
      v = 0;
    unpack8xn(addr + i * 16, airs + (v << 3), 8);
  }
}

static void plot_score()
{
  uint32_t n = state.score;

  GD.__wstart(atxy(26, 19));
  SPI.transfer('0' + (n / 100000) % 10);
  SPI.transfer('0' + (n / 10000) % 10);
  SPI.transfer('0' + (n / 1000) % 10);
  SPI.transfer('0' + (n / 100) % 10);
  SPI.transfer('0' + (n / 10) % 10);
  SPI.transfer('0' + n % 10);
  GD.__end();
}

static void bump_score(byte n)
{
  if ((state.score < 10000) && (10000 <= (state.score + n )))
    state.lives++;
  state.score += n;
  plot_score();
}

static void pause(byte n)
{
  while (n--)
    GD.waitvblank();
}

static void clear_screen(byte yclear = 16)
{
  // blank out top 16 lines
  GD.fill(RAM_CHR, 0, 16);
  GD.wr16(RAM_PAL, BLACK);
  for (byte y = 0; y < yclear; y++)
    GD.fill(atxy(0, y), 0, 32);
  for (int i = 0; i < 256; i++)
    hide(i);
  for (int i = 0; i < 64; i++)
    GD.voice(i, 0, 0, 0, 0);
  pause(6);
}

static void loadlevel(const PROGMEM struct level *l)
{
  clear_screen();

  // border
  GD.wr16(RAM_PAL + 8 * CHR_BORDER, COLOR(pgm_read_byte_near(&l->border)));

  // background picture: uncompress into scratch then copy to screen
  flash_uint8_t *background = (flash_uint8_t*)pgm_read_word_near(&l->background);
  uint16_t scratch = 4096 - 512;  // offscreen scratch
  GD.uncompress(scratch, background);
  for (byte y = 0; y < 16; y++)
    for (byte x = 0; x < 32; x++)
      GD.wr(atxy(x, y), GD.rd(scratch + (y << 5) + x));
  for (byte y = 16; y < 24; y++)
    GD.fill(atxy(0, y), ' ', 32);

  // bg characters
  unpack8xn(RAM_CHR, l->bgchars, sizeof(l->bgchars));
  state.conveyor[0] = pgm_read_byte_near(&l->bgchars[8 * 4 + 0]);
  state.conveyor[1] = pgm_read_byte_near(&l->bgchars[8 * 4 + 3]);

  // bg character palettes
  for (byte c = 0; c < 8; c++) {
    byte attr = pgm_read_byte_near(l->bgattr + c);
    GD.wr16(RAM_PAL + 8 * c, COLOR(PAPER(attr)));
    GD.wr16(RAM_PAL + 8 * c + 6, COLOR(INK(attr)));
  }

  // block 2 is the crumbling floor
  // put the 7 anims of crumbling floor in chars 8-14
  GD.fill(RAM_CHR + 16 * 8, 0, 16 * 7);
  flash_uint8_t *src = l->bgchars + 2 * 8;
  for (byte i = 0; i < 7; i++) {
    byte ch = 8 + i;
    unpack8xn(RAM_CHR + 16 * ch + 2 * (i + 1), src, 7 - i);
    byte attr = pgm_read_byte_near(l->bgattr + 2);
    GD.wr16(RAM_PAL + 8 * ch, COLOR(PAPER(attr)));
    GD.wr16(RAM_PAL + 8 * ch + 6, COLOR(INK(attr)));
  }

  // level name and score line
  for (byte x = 0; x < 32; x++) {
    char scoreline[] = "High Score 000000   Score 000000";
    GD.wr(atxy(x, 16), 0x80 + pgm_read_byte_near(l->name + x));
    GD.wr(atxy(x, 19), scoreline[x]);
  }
  plot_score();

  // AIR line characters
  for (byte i = 0; i < 32; i++)
    GD.wr(atxy(i, 17), CHR_AIR + i);

  // the items are 5 sprites, using colors 16 up
  state.nitems = 0;
  for (byte i = 0; i < 5; i++) {
    byte x = pgm_read_byte_near(&l->items[i].x);
    byte y = pgm_read_byte_near(&l->items[i].y);
    if (x || y) {
      loadspr8(IMG_ITEM + i, l->item, 255, 16 + i);   // items have color 16 up
      sprite(IMG_ITEM + i, x, y, IMG_ITEM + i);
      state.nitems++;
    } else
      hide(IMG_ITEM + i);
  }

  // the portal is a sprite, using colors 32,33
  state.portalattr = pgm_read_byte_near(&l->portalattr);
  loadspr16(IMG_PORTAL, l->portal, 32, 33);
  byte portalx = pgm_read_byte_near(&l->portalx);
  byte portaly = pgm_read_byte_near(&l->portaly);
  sprite(IMG_PORTAL, portalx, portaly, IMG_PORTAL);

  // use sprites for blocks NASTY1 and NASTY2
  byte nc1 = pgm_read_byte_near(l->bgattr + ELEM_NASTY1);
  byte nc2 = pgm_read_byte_near(l->bgattr + ELEM_NASTY2);
  loadspr8(IMG_NASTY1, l->bgchars + ELEM_NASTY1 * 8, 255, INK(nc1));
  loadspr8(IMG_NASTY2, l->bgchars + ELEM_NASTY2 * 8, 255, INK(nc2));
  // now paint sprites over all the NASTY blocks
  {
    byte spr = IMG_NASTY1;
    for (byte y = 0; y < 16; y++)
      for (byte x = 0; x < 32; x++) {
        byte blk = GD.rd(atxy(x, y));
        if (blk == ELEM_NASTY1)
          sprite(spr++, x << 3, y << 3, IMG_NASTY1);
        if (blk == ELEM_NASTY2)
          sprite(spr++, x << 3, y << 3, IMG_NASTY2);
      }
  }

  // air
  state.air = pgm_read_byte_near(&l->air);
  plot_air();

  // Conveyor direction
  state.conveyordir = pgm_read_byte_near(&l->conveyordir);

  // the hguardians
  state.bidir = pgm_read_byte_near(&l->bidir);
  state.guardian = l->guardian;
  guardian *pg;
  for (byte i = 0; i < 8; i++) {
    byte a = pgm_read_byte_near(&l->hguard[i].a);
    guards[i].a = a;
    if (a) {
      byte x = pgm_read_byte_near(&l->hguard[i].x);
      byte y = pgm_read_byte_near(&l->hguard[i].y);
      guards[i].x = x;
      guards[i].y = y;
      guards[i].d = pgm_read_byte_near(&l->hguard[i].d);
      guards[i].x0 = pgm_read_byte_near(&l->hguard[i].x0);
      guards[i].x1 = pgm_read_byte_near(&l->hguard[i].x1);
      guards[i].f = 0;
    } else {
      hide(6 + i);
    }
  }

  pg = &guards[4];  // Use slot 4 for special guardians
  switch (state.level) { // Special level handling
  case 4: // Eugene's lair
    pg->a = 0;  // prevent normal guardian logic
    pg->x = 120;
    pg->y = 0;
    pg->d = -1;
    pg->x0 = 0;
    pg->x1 = 88;
    loadspr16(IMG_GUARD + 4, eugene, 255, 15);
    break;
  case 7:   // Miner Willy meets the Kong Beast
  case 11:  // Return of the Alien Kong Beast
    pg->a = 0;
    pg->x = 120;
    pg->y = 0;
    state.switch1 = 0;
    loadspr8(IMG_SWITCH1, lightswitch, 0, 6);
    sprite(IMG_SWITCH1, 48, 0, IMG_SWITCH1);
    state.switch2 = 0;
    loadspr8(IMG_SWITCH2, lightswitch, 0, 6);
    sprite(IMG_SWITCH2, 144, 0, IMG_SWITCH2);
    break;
  }

  for (byte i = 0; i < MAXRAY; i++)
    state.prevray[i] = 4095;

  // Willy
  state.wx = pgm_read_byte_near(&l->wx);
  state.wy = pgm_read_byte_near(&l->wy);
  state.wf = pgm_read_byte_near(&l->wf);
  state.wd = pgm_read_byte_near(&l->wd);
  state.jumping = 0;
  state.convey = 0;
  state.wyv = 0;
}

static byte rol8(byte b, byte d)  // 8-bit byte rotate left
{
  d &= 7;
  return ((b << d) | (b >> (8 - d))) & 0xff;
}


void setup()
{
  GD.begin();
  setup_control();
  Serial.begin(1000000);    // JCB
}

void game_graphics()
{
  // Load the Spectrum font: regular in 32-127 reverse in 160-255
  unpack8xn(RAM_CHR + 16 * ' ', specfont, sizeof(specfont));
  unpack8xn(RAM_CHR + 16 * (128 + ' '), specfont, sizeof(specfont));
  for (byte i = 32; i < 128; i++) {
    GD.setpal(4 * i,              BLACK);
    GD.setpal(4 * i + 3,          YELLOW);
    GD.setpal(4 * (128 + i),      COLOR(6));    // dark yellow
    GD.setpal(4 * (128 + i) + 3,  BLACK);
  }

  // AIR display in 32 chars starting at CHR_AIR
  unpack8xn(RAM_CHR + 16 * CHR_AIR, specfont + 8 * ('A' - ' '), 8);
  unpack8xn(RAM_CHR + 16 * (CHR_AIR+1), specfont + 8 * ('I' - ' '), 8);
  unpack8xn(RAM_CHR + 16 * (CHR_AIR+2), specfont + 8 * ('R' - ' '), 8);
  for (byte i = 0; i < 32; i++) {
    uint16_t color = (i < 10) ? RED : GREEN;
    GD.setpal(4 * (CHR_AIR + i), color);
    GD.setpal(4 * (CHR_AIR + i) + 3, WHITE);
  }

  // Load the Spectrum's palette into PALETTE256A
  GD.copy(RAM_SPRPAL, specpal, sizeof(specpal));
  GD.wr16(RAM_SPRPAL + 2 * 255, TRANSPARENT);  // color 255 is transparent

  // fill screen with char CHR_BORDER
  GD.fill(RAM_CHR + 16 * CHR_BORDER, 0, 16);
  GD.fill(RAM_PIC, CHR_BORDER, 64 * 64);

  // Load Willy in bright cyan and white
  for (byte i = 0; i < 4; i++) {
    loadspr16(IMG_WILLYC + i, willy + (i << 5), 255, 8 + 5);
  }
}

// midi frequency table
static const PROGMEM uint16_t midifreq[128] = {
32,34,36,38,41,43,46,48,51,55,58,61,65,69,73,77,82,87,92,97,103,110,116,123,130,138,146,155,164,174,184,195,207,220,233,246,261,277,293,311,329,349,369,391,415,440,466,493,523,554,587,622,659,698,739,783,830,880,932,987,1046,1108,1174,1244,1318,1396,1479,1567,1661,1760,1864,1975,2093,2217,2349,2489,2637,2793,2959,3135,3322,3520,3729,3951,4186,4434,4698,4978,5274,5587,5919,6271,6644,7040,7458,7902,8372,8869,9397,9956,10548,11175,11839,12543,13289,14080,14917,15804,16744,17739,18794,19912,21096,22350,23679,25087,26579,28160,29834,31608,33488,35479,37589,39824,42192,44701,47359,50175
};
#define MIDI(n) pgm_read_word(midifreq + (n))

static void squarewave(byte voice, uint16_t f0, byte amp)
{
  GD.voice(8*voice + 0, 0, f0,     amp,    amp);
  GD.voice(8*voice + 1, 0, 3 * f0, amp/3,  amp/3);
  GD.voice(8*voice + 2, 0, 5 * f0, amp/5,  amp/5);
  GD.voice(8*voice + 3, 0, 7 * f0, amp/7,  amp/7);
  GD.voice(8*voice + 4, 0, 9 * f0, amp/9,  amp/9);
  GD.voice(8*voice + 5, 0, 11 * f0, amp/11,  amp/11);
}

static void drive_level(byte t);

static int title_screen() // returns 1 if player pressed something
{
  for (;;) {
display:
    clear_screen();
    GD.fill(0x4000, 0, 6144);   // clear emulated Spectrum video RAM
    GD.uncompress(0x7000, spectrum_tables);

    // fill screen with char 0x80 for border
    GD.setpal(4 * 0x80, COLOR(2));
    GD.fill(RAM_CHR, 0, 4096);
    GD.fill(RAM_PIC, 0x80, 4096);

    // paint the 256x192 window as alternating lines of
    // chars 0-31 and 32-63
    for (byte y = 0; y < 24; y += 2) {
      for (byte x = 0; x < 32; x++) {
        GD.wr(atxy(x, y), x);
        GD.wr(atxy(x, y + 1), 32 + x);
      }
    }
    GD.microcode(spectrum_code, sizeof(spectrum_code));
    GD.uncompress(0x4000, screen_mm1);

    for (const PROGMEM uint32_t *tune = blue_danube;
         (size_t)tune < ((size_t)blue_danube + sizeof(blue_danube));
         tune++) {
      uint32_t cmd = pgm_read_dword_near(tune);
      for (int t = 60 * (cmd >> 28); t; t--) {
        delay(1);
        if (control())
          goto escape;
      }
      byte n0 = 127 & (cmd >> 21);
      byte a0 = 127 & (cmd >> 14);
      byte n1 = 127 & (cmd >> 7);
      byte a1 = 127 & cmd;
      squarewave(0, MIDI(n0), a0 >> 1);
      squarewave(1, MIDI(n1), a1 >> 1);
      GD.setpal(4 * 0x80, COLOR((n1 ^ n0) & 7));
    }
    GD.setpal(4 * 0x80, COLOR(7));
    // GD.screenshot(ss++);    // JCB

    for (byte i = 0; i < ((sizeof(message) - 1) - 32); i++) {
      flash_uint8_t *src = message + i;
      for (byte j = 0; j < 32; j++) {
        byte ch = pgm_read_byte_near(src + j);
        flash_uint8_t *glyph = specfont + ((ch - ' ') << 3);
        for (byte k = 0; k < 8; k++)
          GD.wr(0x5060 + j + (k << 8), pgm_read_byte_near(glyph++));
      }
      for (byte t = 80; t; t--) {
        delay(1);
        if (control())
          goto escape;
      }
    }

    GD.wr(J1_RESET, 1); // stop coprocessor
    clear_screen(24);
    game_graphics();

    for (state.level = 0; state.level < 19; state.level++) {
      loadlevel(&levels[state.level]);
      for (byte t = 0; t < 128; t++) {
        drive_level(t);
        GD.waitvblank();
        if (control())
          goto display;
      }
    }
  }

escape:
  GD.wr(J1_RESET, 1); // stop coprocessor
  clear_screen(24);
  game_graphics();
}

static void game_over()
{
  clear_screen();
  // Willy
  loadspr16(0, willy, 255, 8 + 7);
  sprite(0, 124, 128 - 32, 0);

  // plinth
  loadspr16(2, plinth, 255, 8 + 7);
  sprite(2, 120, 128 - 16, 2);

  // Boot
  loadspr16(1, boot, 255, 8 + 7);
  loadspr16(3, boot, 255, 8 + 7);   // sprite 3 is top 2 lines of boot
  // erase the bottom 14 lines of boot
  GD.fill(RAM_SPRIMG + 3 * 256 + 2 * 16, 255, 14 * 16);

  for (byte i = 0; i <= 48; i++) {
    sprite(1, 120, 2 * i, 1);         // boot in sprite 1
    sprite(3 + i, 120, 2 * i, 3);     // leg in sprites 3,4, etc
    if (41 <= i) // erase Willy as boot covers him
      GD.fill(RAM_SPRIMG + 32 * (i - 41), 255, 32);
    if ((i & 1) == 0)
      GD.wr16(RAM_PAL, COLOR(random(7)));
    squarewave(0, 400 + 4 * i, 150);
    pause(2);
  }
  GD.wr16(RAM_PAL, BLACK);
  squarewave(0, 0, 0);

  // "Game Over" text in sprite images 16-23, color 16-23
  char msg[] = "GameOver";
  for (byte i = 0; i < 8; i++)
    loadspr8(16 + i, specfont + (msg[i] - 32) * 8, 255, 16 + i);
  for (byte i = 0; i < 4; i++) {
    sprite(100 + i, 80 + i * 8, 48, 16 + i);
    sprite(104 + i, 140 + i * 8, 48, 20 + i);
  }
  for (byte t = 120; t; t--) {
    for (byte i = 0; i < 8; i++)
      GD.wr16(RAM_SPRPAL + (16 + i) * 2, COLOR(9 + random(7)));
    pause(2);
  }
  clear_screen();
}

// crumble block at s, which is the sequence
// 2->8->9->10->11->12->13->14->0

static void crumble(uint16_t s)
{
  byte r = GD.rd(s);
  signed char nexts[] = {
    -1, -1,  8, -1, -1, -1, -1, -1,
    9,  10, 11, 12, 13, 14, 0 };
  if (r < sizeof(nexts) && nexts[r] != -1)
    GD.wr(s, nexts[r]);
}

// is it legal for willy to be at (x,y)
static int canbe(byte x, byte y)
{
  uint16_t addr = atxy(x / 8, y / 8);
  return
    (GD.rd(addr) != ELEM_WALL) &&
    (GD.rd(addr+1) != ELEM_WALL) &&
    (GD.rd(addr+64) != ELEM_WALL) &&
    (GD.rd(addr+65) != ELEM_WALL);
}

// is Willy standing in a solar ray?
static int inray(byte x, byte y)
{
  uint16_t addr = atxy(x / 8, y / 8);
  return
    (GD.rd(addr) > 0x80 ) ||
    (GD.rd(addr+1) > 0x80) ||
    (GD.rd(addr+64) > 0x80) ||
    (GD.rd(addr+65) > 0x80);
}

static void drive_level(byte t)
{
  uint16_t freq = 133955L / pgm_read_byte_near(music1 + ((t >> 1) & 63));
  GD.waitvblank();
  squarewave(0, freq, 128);
  GD.waitvblank();
  squarewave(0, freq, 0);
  GD.waitvblank();
  GD.waitvblank();

  guardian *pg;
  for (byte i = 0; i < 8; i++) {
    pg = &guards[i];
    if (pg->a) {
      byte vertical = (i >= 4);
      byte frame;
      switch (state.level) {
      case 13:  // Skylabs
        if (pg->y != pg->x1) {
          pg->f = 0;
          pg->y += pg->d;
        } else {
          pg->f++;
        }
        if (pg->f == 8) {
          pg->f = 0;
          pg->x += 64;
          pg->y = pg->x0;
        }
        loadspr16(IMG_GUARD + i, state.guardian + (pg->f << 5), 255, INK(pg->a));
        sprite(IMG_GUARD + i, pg->x, pg->y, IMG_GUARD + i);
        break;
      default:
        if (state.bidir)
          frame = (t & 3) ^ (pg->d ? 7 : 0);
        else
          if (vertical)
            frame = t & 3;
          else
            frame = 4 + (t & 3) ^ (pg->d ? 3 : 0);
        loadspr16(IMG_GUARD + i, state.guardian + (frame << 5), 255, INK(pg->a));
        sprite(IMG_GUARD + i, pg->x, pg->y, IMG_GUARD + i);
        if (!vertical) {
          if ((t & 3) == 3) {
            if (pg->x == pg->x0 && pg->d)
              pg->d = 0;
            else if (pg->x == pg->x1 && !pg->d)
              pg->d = 1;
            else
              pg->x += pg->d ? -8 : 8;
          }
        } else {
          if (pg->y <= pg->x0 && pg->d < 0)
            pg->d = -pg->d;
          else if (pg->y >= pg->x1 && pg->d > 0)
            pg->d = -pg->d;
          else
            pg->y += pg->d;
        }
      }
    }
  }
  pg = &guards[4];
  switch (state.level) { // Special level handling
  case 4: // Eugene's lair
    sprite(IMG_GUARD + 4, pg->x, pg->y, IMG_GUARD + 4);
    if (pg->y == pg->x0 && pg->d < 0)
      pg->d = 1;
    if (pg->y == pg->x1 && pg->d > 0)
      pg->d = -1;
    if (state.nitems == 0) {  // all collected -> descend and camp
      if (pg->d == -1)
        pg->d = 1;
      if (pg->y == pg->x1)
        pg->d = 0;
    }
    pg->y += pg->d;
    break;
  case 7: // Miner Willy meets the Kong Beast
  case 11: //  Return of the Alien Kong Beast
    byte frame, color;
    if (!state.switch2) {
      frame = (t >> 3) & 1;
      color = 8 + 4;
    } else {
      frame = 2 + ((t >> 1) & 1);
      color = 8 + 6;
      if (pg->y < 104) {
        pg->y += 4;
        bump_score(100);
      }
    }
    if (pg->y != 104) {
      loadspr16(IMG_GUARD + 4, state.guardian + (frame << 5), 255, color);
      sprite(IMG_GUARD + 4, pg->x, pg->y, IMG_GUARD + 4);
    } else {
      hide(IMG_GUARD + 4);
    }
  }

  // Animate conveyors: 
  signed char convt = state.conveyordir ? t : -t;
  GD.__wstart(RAM_CHR + 4 * 16 + 2 * 0);  // character 4 line 0
  unpack8(rol8(state.conveyor[0], convt));
  GD.__end();
  GD.__wstart(RAM_CHR + 4 * 16 + 2 * 3);  // character 4 line 3
  unpack8(rol8(state.conveyor[0], -convt));
  GD.__end();

  // Solar rays
  if (state.level == 18) {
    // Draw new ray, turning 0x00 to 0x80
    byte sx = 23;
    byte sy = 0;
    byte sd = 0; // down
    byte ri;     // ray index
    for (ri = 0; ri < MAXRAY; ri++) {
      uint16_t a = atxy(sx, sy);
      if (state.prevray[ri] != a) {
        GD.wr(state.prevray[ri], 0);
        if (GD.rd(a) != 0)
          break;  // absorbed
        GD.wr(a, 0x80 + ' ');
        state.prevray[ri] = a;
      }
      for (byte i = 0; i < 8; i++)
        if (guards[i].a && (guards[i].x >> 3) == sx && (guards[i].y >> 3) == sy)
          sd ^= 1;
      if (sd == 0)
        sy++;
      else
        sx--;
    }
    while (ri < MAXRAY) {
      GD.wr(state.prevray[ri], 0);
      state.prevray[ri++] = 4095;
    }
  }

  // animate item colors starting at 16
  uint16_t colors[4] = { MAGENTA, YELLOW, CYAN, GREEN };
  for (byte i = 0; i < 5; i++)
    GD.wr16(RAM_SPRPAL + 2 * (i + 16), colors[(i + t) & 3]);

  // when all items collected,
  // flash portal in colors 32,33 - the thing the Spectrum *can* do in hardware
  byte flip = (t >> 3) & (CHEAT_OPEN_PORTAL || state.nitems == 0);
  GD.wr16(RAM_SPRPAL + 2 * (32 ^ flip), COLOR(PAPER(state.portalattr)));
  GD.wr16(RAM_SPRPAL + 2 * (33 ^ flip), COLOR(INK(state.portalattr)));

  // animated lives count, draw up to seven
  for (byte i = 0; i < 7; i++)
    if (i < (state.lives - 1))
      sprite(IMG_WILLYC + i, i << 4, 168, IMG_WILLYC + ((t >> 2) & 3));
    else
      hide(IMG_WILLYC + i);

  GD.waitvblank();
}

// play a complete game
void loop()
{
  title_screen();
  game_graphics();

  state.level = START_LEVEL;
  state.score = 0;
  for (state.lives = 3; state.lives; state.lives--) {
    loadlevel(&levels[state.level]);
    // state.wy = 16;
    byte alive = 1;
    byte t = 0;
    while (alive) {
      drive_level(t);

      // Willy draw
      byte frame = state.wf ^ (state.wd ? 7 : 0);
      loadspr16(IMG_WILLY, willy + (frame << 5), 255, 8 + 7);
      sprite(IMG_WILLY, state.wx, state.wy, IMG_WILLY);

      // Willy movement
      // See http://www.seasip.demon.co.uk/Jsw/manic.mac
      // and http://jetsetwilly.jodi.org/poke.html

      byte con = control();
      byte ychanged = 0;  // if Y block changes must check for landing later
      if (state.jumping) {
#define JUMP_APEX 9   
#define JUMP_FALL 11  //  1   2   3   4   5   6   7   8   9  10 11
        signed char moves[] = {  -4, -4, -3, -3, -2, -2, -1, -1, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
        byte index = min(sizeof(moves) - 1, state.jumping - 1);
        byte newy = state.wy + moves[index];
        state.jumping++;
        if (canbe(state.wx, newy)) {
          ychanged = (state.wy >> 3) != (newy >> 3);
          state.wy = newy;
        } else {
          state.jumping = max(state.jumping, JUMP_FALL);   // halt ascent
          ychanged = 1;
        }
      }
      uint16_t feet_addr = atxy(state.wx >> 3, (state.wy + 16) >> 3);
      byte elem0 = GD.rd(feet_addr);
      byte elem1 = GD.rd(feet_addr + 1);
      byte elem = ((1 <= elem0) && (elem0 <= 31)) ? elem0 : elem1;

      byte dx = 0xff;
      byte first_jump = (con & CONTROL_JUMP) && state.lastdx == 0xff;
      if (state.jumping) {
        dx = state.lastdx;
      } else if (!first_jump && (elem == ELEM_CONVEYOR) && (state.wd == state.conveyordir)) {
        dx = state.conveyordir;
      } else {
        if (con & CONTROL_RIGHT) {
          if (state.wd != 0) {
            state.wf ^= 3;
            state.wd = 0;
          } else {
            dx = 0;
          }
        } else if (con & CONTROL_LEFT) {
          if (state.wd == 0) {
            state.wf ^= 3;
            state.wd = 1;
          } else {
            dx = 1;
          }
        }
      }
      if (dx != 0xff) {
        if (state.wf != 3)
          state.wf++;
        else {
          byte newx = state.wx + (dx ? -8 : 8);
          if (canbe(newx, state.wy)) {
            state.wf = 0;
            state.wx = newx;
          }
        }
      }
      state.lastdx = dx;

      if ((elem == ELEM_CONVEYOR) && (dx == 0xff) && !state.jumping)
        state.wd = state.conveyordir;

      if (!state.jumping && (con & CONTROL_JUMP)) {
        if (canbe(state.wx, state.wy - 3)) {
          state.jumping = 1;
          state.wy -= 0;
        }
      }

      byte onground = ((1 <= elem) && (elem <= 4)) || ((7 <= elem) && (elem <= 16));
      if (state.jumping) {
        if ((JUMP_APEX <= state.jumping) && ychanged && onground) {
          state.jumping = 0;
          state.wy &= ~7;
        }
        if (state.jumping >= 16)    // stop x movement late in the jump
          state.lastdx = 0xff;
      } else {
        if (!onground) {    // nothing to stand on, start falling
          state.jumping = JUMP_FALL;
          state.lastdx = 0xff;
        }
      }
      if (!state.jumping) {
        crumble(feet_addr);
        crumble(feet_addr + 1);
      }

      if (((t & 7) == 0) ||
         ((state.level == 18) && inray(state.wx, state.wy))) {
        state.air--;
        plot_air();
        if (state.air == 0) {
          alive = 0;
        }
      }
      GD.waitvblank();    // let the screen display, then check for collisions
      byte coll = GD.rd(COLLISION + IMG_WILLY);
      if (coll <= (IMG_ITEM + 4)) {
        bump_score(100);
        hide(coll - IMG_ITEM);
        --state.nitems;
      } else if (coll == IMG_PORTAL) {
        if (CHEAT_OPEN_PORTAL || state.nitems == 0) {
          while (state.air) {
            squarewave(0, 800 + 2 * state.air, 100);
            state.air--;
            plot_air();
            bump_score(7);
            GD.waitvblank();
          }
          state.level = (state.level + 1) % 18;
          loadlevel(&levels[state.level]);
        }
      } else if (coll == IMG_SWITCH1) {
        if (!state.switch1) {
          state.switch1 = 1;
          sprite(IMG_SWITCH1, 48 - 8, 0, IMG_SWITCH1, 2);
          GD.wr(atxy(17, 11), 0);
          GD.wr(atxy(17, 12), 0);
        }
      } else if (coll == IMG_SWITCH2) {
        if (coll == IMG_SWITCH2) {
          state.switch2 = 1;
          sprite(IMG_SWITCH2, 144 - 8, 0, IMG_SWITCH2, 2);
          GD.wr(atxy(15, 2), 0);
          GD.wr(atxy(16, 2), 0);
        }
      } else if (coll != 0xff && !CHEAT_INVINCIBLE) {
        alive = 0;
      }
      t++;
    }
    // player died
    clear_screen();
  }
  game_over();
}
