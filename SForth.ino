#include "SForth.h"

extern "C"
{ 
  void outputString(char *str)
  {
    Serial.print(str);
  }
}
extern "C" void SForthBegin(void(*func)(char*));
char lineBuf[1000];
char *cptr;
void setup() {
  Serial.begin(9600);
  delay(2000);

  SForth.begin(&outputString);

  cptr = lineBuf;
}

void loop() {
  // put your main code here, to run repeatedly:
  if (Serial.available())
  {
    *cptr = Serial.read();
    if (*cptr == 0xd || *cptr == 0xa)
    {
      *cptr = '\0';
      SForth.evaluate(lineBuf);
      cptr = lineBuf;
    }
    else
      ++cptr;
  }
}
