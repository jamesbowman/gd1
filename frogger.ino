#include <SPI.h>
#include <GD.h>

// Background http://gameduino.com/results/5f30d40b/
// Sprites    http://gameduino.com/results/1d2bd1ca/

#include "froggerbg.h"
#include "sprite.h"

#define CONTROL_LEFT  1
#define CONTROL_RIGHT 2
#define CONTROL_UP    4
#define CONTROL_DOWN  8

class Controller {
  public:
    void begin() {
      byte i;
      for (i = 3; i < 7; i++) {
        pinMode(i, INPUT);
        digitalWrite(i, HIGH);
      }
      prev = 0;
    }
    byte read() {
      byte r = 0;
      if (!digitalRead(5))
        r |= CONTROL_DOWN;
      if (!digitalRead(4))
        r |= CONTROL_UP;
      if (!digitalRead(6))
        r |= CONTROL_LEFT;
      if (!digitalRead(3))
        r |= CONTROL_RIGHT;
      byte edge = r & ~prev;
      prev = r;
      return edge;
    }
  private:
    byte prev;
};

static Controller Control;

#define BG_BLUE   0
#define BG_ZERO   12
#define BG_BLACK  34
#define BG_LIFE   35

// JCB-FROG3-ATXY0
static uint16_t atxy(byte x, byte y)
{
  return RAM_PIC + 64 * y + (x + 11);
}
// JCB-FROG3-ATXY1

static void draw_score(uint16_t dst, long n)
{
  GD.wr(dst + 0, BG_ZERO + (n / 10000) % 10);   // ten-thousands
  GD.wr(dst + 1, BG_ZERO + (n / 1000) % 10);    // thousands
  GD.wr(dst + 2, BG_ZERO + (n / 100) % 10);     // hundreds
  GD.wr(dst + 3, BG_ZERO + (n / 10) % 10);      // tens
  GD.wr(dst + 4, BG_ZERO + n % 10);             // ones
}

// Game variables
static unsigned int t;
// JCB-FROG3-VARSA
static int frogx, frogy;    // screen position
static int leaping;         // 0 means not leaping, 1-8 animates the leap
static int frogdir;         // while leaping, which direction is the leap?
static int frogface;        // which way is the frog facing, as a sprite ROT field
static int dying;           // 0 means not dying, 1-64 animation counter
// JCB-FROG3-VARSB
static long score;
static long hiscore;
static byte lives;
static byte done[5];
static byte homes[5] = { 24, 72, 120, 168, 216 };

// JCB-FROG3-FROGSTART-0
void frog_start()
{
  frogx = 120;
  frogy = 232;
  leaping = 0;
  frogdir = 0;
  frogface = 0;
  dying = 0;
}
// JCB-FROG3-FROGSTART-1

void level_start()
{
  for (byte i = 0; i < 5; i++)
    done[i] = 0;

  GD.begin();
  delay(500);
  GD.copy(RAM_CHR, froggerbg_chr, sizeof(froggerbg_chr));
  GD.copy(RAM_PAL, froggerbg_pal, sizeof(froggerbg_pal));
  GD.fill(RAM_PIC, BG_BLACK, 4096);
  for (byte y = 0; y < 32; y++)
    GD.copy(atxy(0, y), froggerbg_pic + y * 28, 28);
  GD.fill(1 * 64 + 11, BG_BLUE, 28);

// JCB-FROG3-CURTAINS0
  byte i = 255;
  for (int y = 0; y < 256; y += 16) {
    GD.sprite(i--, 72, y, 63, 0);
    GD.sprite(i--, 72 + 240, y, 63, 0);
    GD.sprite(i--, 72 + 256, y, 63, 0);
  }
// JCB-FROG3-CURTAINS1

  GD.copy(PALETTE16A, sprite_sprpal, sizeof(sprite_sprpal));
  GD.uncompress(RAM_SPRIMG, sprite_sprimg);
}

void game_start()
{
  lives = 4;
  score = 0;
}

void setup()
{
  Serial.begin(1000000); // JCB
  Control.begin();

  game_start();
  level_start();
  frog_start();
}

// JCB-FROG3-SPRITE0
static void sprite(byte x, byte y, byte anim, byte rot = 0)
{
  draw_sprite(x + 80, y, anim, rot);
}
// JCB-FROG3-SPRITE1

static void turtle3(byte x, byte y)
{
  byte anim = 50 + ((t / 32) % 3);
  sprite(x, y, anim);
  sprite(x + 16, y, anim);
  sprite(x + 32, y, anim);
}

static void turtle2(byte x, byte y)
{
  byte anim = 50 + ((t / 32) % 3);
  sprite(x, y, anim);
  sprite(x + 16, y, anim);
}

void log1(byte x, byte y)
{
  sprite(x, y,      86);
  sprite(x + 16, y, 87);
  sprite(x + 32, y, 88);
}

void log(byte length, byte x, byte y)
{
  sprite(x, y,      86);
  while (length--) {
    x += 16;
    sprite(x, y, 87);
  }
  sprite(x + 16, y, 88);
}

static int riverat(byte y, uint16_t tt)
{
  switch (y) {
  case 120: return -tt;
  case 104: return tt;
  case 88:  return 5 * tt / 4;
  case 72:  return -tt / 2;
  case 56:  return tt / 2;
  }
}

// JCB-FROG3-SND0
// midi frequency table
static const PROGMEM uint16_t midifreq[128] = {
32,34,36,38,41,43,46,48,51,55,58,61,65,69,73,77,82,87,92,97,103,110,116,123,130,138,146,155,164,174,184,195,207,220,233,246,261,277,293,311,329,349,369,391,415,440,466,493,523,554,587,622,659,698,739,783,830,880,932,987,1046,1108,1174,1244,1318,1396,1479,1567,1661,1760,1864,1975,2093,2217,2349,2489,2637,2793,2959,3135,3322,3520,3729,3951,4186,4434,4698,4978,5274,5587,5919,6271,6644,7040,7458,7902,8372,8869,9397,9956,10548,11175,11839,12543,13289,14080,14917,15804,16744,17739,18794,19912,21096,22350,23679,25087,26579,28160,29834,31608,33488,35479,37589,39824,42192,44701,47359,50175
};
#define MIDI(n) pgm_read_word(midifreq + (n))

static void squarewave(uint16_t freq, byte amp)
{
  GD.voice(0, 0, freq,     amp,    amp);
  GD.voice(1, 0, 3 * freq, amp/3,  amp/3);
  GD.voice(2, 0, 5 * freq, amp/5,  amp/5);
  GD.voice(3, 0, 7 * freq, amp/7,  amp/7);
  GD.voice(4, 0, 9 * freq, amp/9,  amp/9);
  GD.voice(5, 0, 11 * freq, amp/11,  amp/11);
}

static void sound()
{
  byte note;

  if (dying) { 
    note = 84 - (dying / 2);
    squarewave(MIDI(note), 100);
  } else if (leaping) {
    if (leaping & 1)
      note = 60 + leaping;
    else
      note = 72 + leaping;
    squarewave(MIDI(note), 100);
  } else {
    squarewave(0, 0);  // silence
  }
}
// JCB-FROG3-SND1

void loop()
{
  GD.__wstartspr(0);

  // Completed homes
  for (byte i = 0; i < 5; i++) {
    if (done[i])
      sprite(homes[i], 40, 63);
  }

  // Yellow cars
  sprite(-t,       216, 3);
  sprite(-t + 128, 216, 3);

  // Dozers
  sprite(t, 200, 4);
  sprite(t + 50, 200, 4);
  sprite(t + 150, 200, 4);

  // Purple cars
  sprite(-t,       184, 7);
  sprite(-t + 75,  184, 7);
  sprite(-t + 150, 184, 7);

  // Green and white racecars
  sprite(2 * t,    168, 8);

  // Trucks
  sprite(-t/2,       152, 5);
  sprite(-t/2 + 16,  152, 6);
  sprite(-t/2 + 100, 152, 5);
  sprite(-t/2 + 116, 152, 6);

  // Turtles
  for (int i = 0; i < 256; i += 64)
    turtle3(riverat(120, t) + i, 120);

  // Short logs
  for (int i = 0; i < 240; i += 80)
    log(1, riverat(104, t) + i, 104);

  // Long logs
  for (int i = 0; i < 256; i += 128)
    log(5, riverat(88, t) + i, 88);

  // Turtles again, but slower
  for (int i = 0; i < 250; i += 50)
    turtle2(riverat(72, t) + i, 72);

  // Top logs
  for (int i = 0; i < 210; i += 70)
    log(2, riverat(56, t) + i, 56);

// JCB-FROG3-DRAW-0
  // The frog himself, or his death animation
  byte frogspr = GD.spr; // record which sprite slot frog got, for collision check below

  if (!dying) {
    static byte frog_anim[] = {2, 1, 0, 0, 2};
    sprite(frogx, frogy, frog_anim[leaping / 2], frogface);
  } else {
    static byte die_anim[] = {31, 32, 33, 30};
    sprite(frogx, frogy, die_anim[dying / 16], frogface);
  }
// JCB-FROG3-DRAW-1

  GD.__end();
  // GD.screenshot(t);  // JCB
  t++;

// JCB-FROG3-E
  // player control.  If button pressed, start the 'leaping' counter
  byte con = Control.read();
  if (!dying && (leaping == 0) && con) {
    frogdir = con;
    leaping = 1;
    score += 10;
  } else if (leaping > 0) {
    if (leaping <= 8) {
      if (frogdir == CONTROL_LEFT) {
        frogx -= 2;
        frogface = 3;
      } if (frogdir == CONTROL_RIGHT) {
        frogx += 2;
        frogface = 5;
      } if (frogdir == CONTROL_UP) {
        frogy -= 2;
        frogface = 0;
      } if (frogdir == CONTROL_DOWN) {
        frogy += 2;
        frogface = 6;
      }
      leaping++;
    } else {
      leaping = 0;
    }
  }
// JCB-FROG3-F
  
// JCB-FROG3-COLLIDE-0
  GD.waitvblank();
  byte touching = (GD.rd(COLLISION + frogspr) != 0xff);
// JCB-FROG3-COLLIDE-1

// JCB-FROG3-G
  if (dying) {
    if (++dying == 64) {
      if (--lives == 0) {
        game_start();
        level_start();
      }
      frog_start();
    }
  }
  else if (frogx < 8 || frogx > 224) {
    dying = 1;
  }
// JCB-FROG3-H
// JCB-FROG3-I
  else if (frogy >= 136) {    // road section
    // if touching something, frog dies
    if (touching)
      dying = 1;
  }
// JCB-FROG3-J
  else if (frogy > 40) {      // river section
    if (!leaping) {
      // if touching something, frog is safe
      if (touching) {
        // move frog according to lane speed
        int oldx = riverat(frogy, t - 1);
        int newx = riverat(frogy, t);
        int river_velocity = newx - oldx;
        frogx += river_velocity;
      } else {
        dying = 1;
      }
    }
  }
  else 
// JCB-FROG3-K
  {                      // riverbank section
    if (!leaping) {
      byte landed = 0;
      for (byte i = 0; i < 5; i ++) {
        if (!done[i] && abs(homes[i] - frogx) < 4) {
          done[i] = 1;
          landed = 1;
          score += 10;
        }
      }
      if (landed) {
        if (done[0] && done[1] && done[2] && done[3] && done[4])
          level_start();
        frog_start();
      } else // if frog did not land in a home, die!
        dying = 1;
    }
  }
// JCB-FROG3-L
  sound();
  hiscore = max(score, hiscore);
  draw_score(atxy(3, 1), score);
  draw_score(atxy(11, 1), hiscore);
  for (byte i = 0; i < 16; i++)
    GD.wr(atxy(i, 30), (i < lives) ? BG_LIFE : BG_BLACK);
}
