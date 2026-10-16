#include "stream_protocol.h"
#include <cassert>
#include <string>
#include <iostream>

int main() {
    using namespace stream;
    assert(fresh(1000,251000,250));
    assert(!fresh(1000,251001,250));
    assert(!fresh(0,1000,250));
    assert(!fresh(2000,1000,250));
    const uint64_t longUptime=uint64_t(1)<<40;
    assert(fresh(longUptime,longUptime+1000,250));
    char header[360];
    int n=partHeader(header,sizeof header,"FRONT",0x1234abcd,4294967297ULL,longUptime,12345);
    assert(n>0 && size_t(n)==strlen(header));
    std::string h(header);
    assert(h.find("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: 12345\r\n")==0);
    assert(h.find("X-Camera-Node: FRONT\r\n")!=std::string::npos);
    assert(h.find("X-Frame-Index: 4294967297\r\n")!=std::string::npos);
    assert(h.find("X-Boot-Id: 1234abcd\r\n")!=std::string::npos);
    assert(h.substr(h.size()-4)=="\r\n\r\n");
    assert(partHeader(header,8,"REAR",1,1,1,1)==-1);
    assert(route("GET /stream HTTP/1.1")==Route::Stream);
    assert(route("GET /sync HTTP/1.0")==Route::Sync);
    assert(route("GET /status HTTP/1.1")==Route::Status);
    assert(route("POST /stream HTTP/1.1")==Route::MethodNotAllowed);
    assert(route("GET /unknown HTTP/1.1")==Route::NotFound);
    assert(route("GET /stream")==Route::BadRequest);
    assert(route("GET /stream HTTP/1.1 extra")==Route::BadRequest);
    assert(route("GET /stream XXX")==Route::BadRequest);
    const uint8_t bytes[]={1,2,3,4,5};uint64_t now=0;std::string output;int calls=0;
    auto clock=[&]{return now;};auto yield=[&]{++now;};
    auto partial=[&](const uint8_t* p,size_t size) {
        if(++calls%2==0)return 0;
        size_t count=size>2?2:size;output.append(reinterpret_cast<const char*>(p),count);return int(count);
    };
    assert(writeAll(bytes,5,10,partial,clock,yield));
    assert(output==std::string(reinterpret_cast<const char*>(bytes),5));
    assert(!writeAll(bytes,5,5,[](const uint8_t*,size_t){return 0;},clock,yield));
    assert(now==5); // Stalled writer exits at the absolute budget, not a per-chunk budget.
    calls=0;
    assert(!writeAll(bytes,5,now,[&](const uint8_t*,size_t){++calls;return 5;},clock,yield));
    assert(calls==0);
    assert(!writeAll(bytes,5,10,[](const uint8_t*,size_t){return -1;},clock,yield));
    assert(!writeAll(bytes,5,10,[](const uint8_t*,size_t){return 6;},clock,yield));
    std::cout<<"PASS: freshness, multipart metadata, routing, partial writes and send deadlines\n";
}
