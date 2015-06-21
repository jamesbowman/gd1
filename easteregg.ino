#include <SPI.h>
void setup() {
  delay(10000);
  pinMode(9, OUTPUT);
  digitalWrite(9, HIGH);
  SPI.begin();
  digitalWrite(9, LOW);
  SPI.transfer(0xa8);
  SPI.transfer(0x99);
  SPI.transfer(0x73);
  digitalWrite(9, HIGH);
}
void loop() { }
