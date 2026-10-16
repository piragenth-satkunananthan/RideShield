#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace stream {
inline bool fresh(uint64_t captureUs,uint64_t nowUs,unsigned maxAgeMs) {
    return captureUs>0 && captureUs<=nowUs && nowUs-captureUs<=uint64_t(maxAgeMs)*1000;
}
inline int partHeader(char* out,size_t capacity,const char* node,uint32_t boot,
                      uint64_t index,uint64_t captureUs,size_t bytes) {
    const int n=snprintf(out,capacity,
        "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n"
        "X-Camera-Node: %s\r\nX-Boot-Id: %08lx\r\nX-Frame-Index: %llu\r\n"
        "X-Capture-Timestamp-Us: %llu\r\nX-Timestamp-Source: driver-first-dma\r\n\r\n",
        unsigned(bytes),node,static_cast<unsigned long>(boot),
        static_cast<unsigned long long>(index),static_cast<unsigned long long>(captureUs));
    return n>0 && size_t(n)<capacity?n:-1;
}
// Nonblocking writer returns positive=bytes accepted, zero=retry, negative=error.
// One absolute deadline covers the whole multipart frame, including its JPEG.
template<class Writer,class Clock,class Yield>
bool writeAll(const uint8_t* bytes,size_t length,uint64_t deadline,Writer writer,Clock clock,Yield yield) {
    size_t sent=0;
    while(sent<length) {
        if(clock()>=deadline)return false;
        const int n=writer(bytes+sent,length-sent);
        if(n<0 || size_t(n)>length-sent)return false;
        if(n==0)yield();else sent+=size_t(n);
    }
    return true;
}
enum class Route { Stream,Sync,Status,Index,NotFound,MethodNotAllowed,BadRequest };
inline Route route(const char* line) {
    char method[8],path[64],version[16],extra;
    if(sscanf(line,"%7s %63s %15s %c",method,path,version,&extra)!=3)return Route::BadRequest;
    if(strcmp(version,"HTTP/1.1") && strcmp(version,"HTTP/1.0"))return Route::BadRequest;
    if(strcmp(method,"GET"))return Route::MethodNotAllowed;
    if(!strcmp(path,"/stream"))return Route::Stream;
    if(!strcmp(path,"/sync"))return Route::Sync;
    if(!strcmp(path,"/status"))return Route::Status;
    if(!strcmp(path,"/"))return Route::Index;
    return Route::NotFound;
}
}
