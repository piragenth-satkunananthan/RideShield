#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <initializer_list>
#define LOW 0
#define HIGH 1
#define INPUT_PULLUP 2
#define OUTPUT 3
extern uint64_t fakeMs;
extern int inputPins[49],outputPins[49];
struct FakeSerial{
 std::string text;
 void begin(int){}
 int available(){return 0;}
 int read(){return -1;}
 void println(const char* s){text+=s;text+='\n';}
 template<class... A> void printf(const char* f,A... a){char b[512];std::snprintf(b,sizeof b,f,a...);text+=b;}
};
extern FakeSerial Serial;
inline void pinMode(int p,int mode){if(mode==INPUT_PULLUP)inputPins[p]=HIGH;}
inline int digitalRead(int p){return inputPins[p];}
inline void digitalWrite(int p,int v){outputPins[p]=v;}
inline void delay(unsigned){}
