#include "Arduino.h"
#include <cassert>
#include <iostream>
uint64_t fakeMs=1000;
int inputPins[49]{},outputPins[49]{};
FakeSerial Serial;
void setup();void loop();
void run(unsigned ms){for(unsigned i=0;i<ms;i+=5){fakeMs+=5;loop();}}
int main(){
 setup();run(50);inputPins[4]=LOW;run(5);assert(outputPins[39]==HIGH&&outputPins[40]==HIGH);inputPins[4]=HIGH;
 assert(Serial.text.find("Countdown")!=std::string::npos);std::cout<<"PASS impact button starts alert outputs\n";
 inputPins[21]=LOW;run(1100);inputPins[21]=HIGH;run(50);assert(Serial.text.find("Cancelled")!=std::string::npos);assert(outputPins[39]==LOW);std::cout<<"PASS cancel hold clears countdown\n";
 inputPins[14]=LOW;run(3100);inputPins[14]=HIGH;run(50);assert(Serial.text.find("Emergency")!=std::string::npos);assert(Serial.text.find("SMS PREVIEW ONLY: TEST")!=std::string::npos);assert(Serial.text.find("SmsDisabled")!=std::string::npos);std::cout<<"PASS SOS hold produces preview-only SMS\n";
}
