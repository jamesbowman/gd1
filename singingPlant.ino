/*
 * This is singingPlant from the this instructable:
 * http://www.instructables.com/id/Singing-plant-Make-your-plant-sing-with-Arduino-/
 *
 */
#include <SPI.h>
#include <GD.h>

#include "mont.h"

long started =0;
int ticks = 0;
const uint8_t *pc;

// visualize voice (v) at amplitude (a) on an 8x8 grid

#define SET(x,y) (x |=(1<<y))       //-Bit set/clear macros
#define CLR(x,y) (x &= (~(1<<y)))   // |
#define CHK(x,y) (x & (1<<y))       // |
#define TOG(x,y) (x^=(1<<y))        //-+

#define N 120                       // How many frequencies

float results[N];                   // Filtered result buffer
int sizeOfArray = N;
int fixedGraph = 0;
int topPoint = 0;
int topPointValue = 0;
int topPointInterPolated = 0;
int baseline = 0;
int baselineMax =0;
int value = 0;

void visualize(byte v, byte a)
{
  int x = 64 + ((v & 7) * 34);
  int y = 14 + ((v >> 3) * 34);
  byte sprnum = (v << 2); // draw each voice using four sprites
  GD.sprite(sprnum++, x + 16, y + 16, a, 0, 0);
  GD.sprite(sprnum++, x +  0, y + 16, a, 0, 2);
  GD.sprite(sprnum++, x + 16, y +  0, a, 0, 4);
  GD.sprite(sprnum++, x +  0, y +  0, a, 0, 6);
}

void setup()
{
  gBegin(34526);
  // max and min buttons

  pinMode(A1, INPUT);
  pinMode(A2, INPUT);
  digitalWrite(A1, HIGH);
  digitalWrite(A2, HIGH);
  int i;

  GD.begin(4);

  GD.wr16(RAM_SPRPAL + 2 * 255, TRANSPARENT);

  // draw 32 circles into 32 sprite images
  for (i = 0; i < 32; i++) {
    GD.wr16(RAM_SPRPAL + 2 * i, RGB(8 * i, 64, 255 - 8 * i));
    int dst = RAM_SPRIMG + 256 * i;
    GD.__wstart(dst);
    byte x, y;
    int r2 = min(i * i, 256);
    for (y = 0; y < 16; y++) {
      for (x = 0; x < 16; x++) {
        byte pixel;
        if ((x * x + y * y) <= r2)
          pixel = i;    // use color above
        else
          pixel = 0xff; // transparent
        SPI.transfer(pixel);
      }
    }
    GD.__end();
  }

  for (i = 0; i < 64; i++)
    visualize(i, 0);

  pc = mont;

  TCCR1A=0b10000010;        //-Set up frequency generator
  TCCR1B=0b00011001;        //-+

  ICR1=110;
  OCR1A=55;

  pinMode(9,OUTPUT);        //-Signal generator pin

  for(int i=0;i<N;i++)      //-Preset results
    results[i]=0;         //-+
}

byte amp[64];       // current voice amplitude
byte target[64];    // target voice amplitude

// Set volume for voice v to a
void setvol(byte v, byte a)
{
  GD.__wstart(VOICES + (v << 2) + 2);
  SPI.transfer(a);
  SPI.transfer(a);
  GD.__end();
}

void adsr()  // handle ADSR for 64 voices
{
  byte v;
  for (v = 0; v < 64; v++) {
    int d = target[v] - amp[v]; // +ve means need to increase
    if (d) {
      if (d > 0)
        amp[v] += 4;  // attack
      else
        amp[v]--;     // decay
      if(value < 5)
      {
        setvol(v, 0);
      }
      else
      {
        setvol(v, amp[v]*((float)value/800+0.2));
      }
      visualize(v, amp[v]);
    }
  }
}

unsigned int d=0;

boolean btnMax = false;
boolean btnMin = false;

void loop()
{
  guino_update();
  if (pc >= mont + sizeof(mont) - 4) {
    started = millis();
    ticks = 0;
    pc = mont;
  }
  if (millis() < (started + ticks * 5)) {
    adsr();
  } else {
    byte cmd = pgm_read_byte_near(pc++);        // upper 2 bits are command code
    if ((cmd & 0xc0) == 0) {
      // Command 0x00: pause N*5 milliseconds
      ticks += (cmd & 63);
    } else {
      byte v = (cmd & 63);
      byte a;
      if ((cmd & 0xc0) == 0x40) {
        // Command 0x40: silence voice
        target[v] = 0;
      } else if ((cmd & 0xc0) == 0x80) {
        // Command 0x80: set voice frequency and amplitude
        byte flo = pgm_read_byte_near(pc++);
        if (value < 0)
          flo = 0;
        byte fhi = pgm_read_byte_near(pc++) * ((float) value / 300.0f);
        GD.__wstart(VOICES + 4 * v);
        SPI.transfer(flo);
        SPI.transfer(fhi);
        GD.__end();
        target[v] = pgm_read_byte_near(pc++);
      }
    }
  }

  if (!(d < N)) {
    d = 0;
    topPoint = 0;
    topPointValue = 0;
  }
  int v = analogRead(0);        //-Read response signal
  results[d] = results[d] * 0.5 + (float) (v) * 0.5;    //Filter results
  fixedGraph = round(results[d]);
  gUpdateValue(&fixedGraph);
  if (topPointValue < results[d]) {
    topPointValue = results[d];
    topPoint = d;
  }

  CLR(TCCR1B, 0);               //-Stop generator
  TCNT1 = 0;                    //-Reload new frequency
  ICR1 = d;                     // |
  OCR1A = d / 2;                //-+
  SET(TCCR1B, 0);               //-Restart generator

  d++;
  if (d == N) {
    topPointInterPolated = topPointInterPolated * 0.5f +
        ((topPoint +
          results[topPoint] / results[topPoint + 1] * results[topPoint - 1] /
          results[topPoint]) * 10.0f) * 0.5f;
    value =
        max(0, map(topPointInterPolated, baseline, baselineMax, 0, 1000));
    gUpdateValue(&topPoint);
    gUpdateValue(&value);
    gUpdateValue(&topPointInterPolated);

    if (!digitalRead(A1) && btnMin) {
      baseline = topPointInterPolated;
      gUpdateValue(&baseline);
    }

    btnMin = !digitalRead(A1);

    if (!digitalRead(A2) && btnMax) {
      baselineMax = topPointInterPolated;
      gUpdateValue(&baselineMax);
    }

    btnMax = !digitalRead(A2);
  }
}

// This is where you setup your interface
void gInit()
{
  gAddLabel("DisneyTouch", 1);

  gAddSpacer(1);

  gAddSpacer(1);
  gAddFixedGraph("FIXED GRPAPH", -500, 1000, N, &fixedGraph, 40);
  gAddSlider(0, N, "TOP", &topPoint);
  gAddSlider(0, N * 10, "Interpolated", &topPointInterPolated);
  gAddSlider(0, 3000, "Baseline", &baseline);
  gAddSlider(0, 3000, "BaselineMax", &baselineMax);
  gAddSlider(0, 1000, "Value", &value);
}

// Method called everytime a button has been pressed in the interface.
void gButtonPressed(int id)
{

}

void gItemUpdated(int id)
{

}
