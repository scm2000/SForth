#include "SForth.h"

void setup() {
  Serial.begin(9600);
  delay(2000);

  SForth.begin();

}


void loop() {
  SForth.shellHook();
}
