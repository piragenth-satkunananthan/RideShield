#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_timer.h>
#include <lwip/sockets.h>
#include <atomic>
#include <algorithm>
#include <cerrno>
#include "camera_config.h"
#include "stream_protocol.h"

namespace {
WiFiServer server(8080);
WiFiClient pendingClient;
SemaphoreHandle_t clientMutex=nullptr;
TaskHandle_t streamTask=nullptr;
std::atomic<bool> streaming{false};
std::atomic<uint32_t> networkEpoch{0},acquired{0},delivered{0},stale{0},captureErrors{0},sendErrors{0};
bool cameraReady=false,networkConfigured=false,listening=false;
esp_err_t cameraError=ESP_FAIL;
uint32_t bootId=0;
uint64_t clockUs(){return uint64_t(esp_timer_get_time());}

bool sendBytes(WiFiClient& client,const void* data,size_t size,uint64_t deadline) {
    const int fd=client.fd();
    return stream::writeAll(static_cast<const uint8_t*>(data),size,deadline,
        [fd](const uint8_t* p,size_t n) {
            const int count=::send(fd,p,std::min(n,size_t(1460)),MSG_DONTWAIT);
            if(count<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return 0;
            return count==0?-1:count;
        },clockUs,[]{vTaskDelay(1);});
}
void response(WiFiClient& client,const char* status,const char* type,const char* body) {
    char header[240];
    const int n=snprintf(header,sizeof header,"HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",status,type,unsigned(strlen(body)));
    const auto deadline=clockUs()+200000;
    if(n>0 && size_t(n)<sizeof header && sendBytes(client,header,n,deadline))sendBytes(client,body,strlen(body),deadline);
    client.stop();
}
bool initCamera() {
    if(!psramFound()){Serial.println("CAMERA ERROR: PSRAM missing; streaming disabled");return false;}
    camera_config_t c{};
    c.ledc_channel=LEDC_CHANNEL_0;c.ledc_timer=LEDC_TIMER_0;
    c.pin_d0=board::D0;c.pin_d1=board::D1;c.pin_d2=board::D2;c.pin_d3=board::D3;
    c.pin_d4=board::D4;c.pin_d5=board::D5;c.pin_d6=board::D6;c.pin_d7=board::D7;
    c.pin_xclk=board::XCLK;c.pin_pclk=board::PCLK;c.pin_vsync=board::VSYNC;c.pin_href=board::HREF;
    c.pin_sccb_sda=board::SIOD;c.pin_sccb_scl=board::SIOC;c.pin_pwdn=board::PWDN;c.pin_reset=board::RESET;
    c.xclk_freq_hz=20000000;c.pixel_format=PIXFORMAT_JPEG;c.frame_size=FRAMESIZE_VGA;
    c.jpeg_quality=RS_CAMERA_JPEG_QUALITY;c.fb_count=2;
    c.fb_location=CAMERA_FB_IN_PSRAM;c.grab_mode=CAMERA_GRAB_LATEST;
    cameraError=esp_camera_init(&c);
    if(cameraError!=ESP_OK){Serial.printf("CAMERA init failed: 0x%x\n",cameraError);return false;}
    sensor_t* sensor=esp_camera_sensor_get();
    if(!sensor || sensor->id.PID!=OV2640_PID) {
        Serial.println("CAMERA ERROR: expected OV2640");esp_camera_deinit();cameraError=ESP_ERR_NOT_SUPPORTED;return false;
    }
    if(sensor->set_hmirror(sensor,RS_CAMERA_HMIRROR)!=0 || sensor->set_vflip(sensor,RS_CAMERA_VFLIP)!=0) {
        esp_camera_deinit();cameraError=ESP_FAIL;return false;
    }
    Serial.printf("CAMERA OV2640 ready: VGA JPEG quality=%d target=%d fps PSRAM=%u\n",RS_CAMERA_JPEG_QUALITY,RS_CAMERA_FPS,ESP.getPsramSize());
    return true;
}
void streamWorker(void*) {
    uint64_t index=0;
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        xSemaphoreTake(clientMutex,portMAX_DELAY);
        WiFiClient client=pendingClient;pendingClient=WiFiClient();
        xSemaphoreGive(clientMutex);
        const uint32_t epoch=networkEpoch.load();
        static const char header[]="HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        bool ok=sendBytes(client,header,sizeof(header)-1,clockUs()+200000);
        uint64_t lastTimestamp=0;
        while(ok && client.connected() && WiFi.status()==WL_CONNECTED && networkEpoch.load()==epoch) {
            const auto cycleStart=clockUs();
            camera_fb_t* frame=esp_camera_fb_get();
            if(!frame){captureErrors++;break;}
            ++index;acquired++;
            const uint64_t timestamp=uint64_t(frame->timestamp.tv_sec)*1000000+frame->timestamp.tv_usec;
            if(frame->format!=PIXFORMAT_JPEG || frame->len<4 || frame->buf[0]!=0xff || frame->buf[1]!=0xd8) {
                captureErrors++;esp_camera_fb_return(frame);break;
            }
            if(!stream::fresh(timestamp,clockUs(),board::MAX_FRAME_AGE_MS) || timestamp<=lastTimestamp) {
                stale++;esp_camera_fb_return(frame);vTaskDelay(1);continue;
            }
            lastTimestamp=timestamp;
            char part[360];const int n=stream::partHeader(part,sizeof part,board::NODE,bootId,index,timestamp,frame->len);
            const auto deadline=clockUs()+uint64_t(board::SEND_BUDGET_MS)*1000;
            ok=n>0 && sendBytes(client,part,n,deadline) && sendBytes(client,frame->buf,frame->len,deadline) && sendBytes(client,"\r\n",2,deadline);
            esp_camera_fb_return(frame);
            if(!ok){sendErrors++;break;} // Close partial JPEG; never append another part to a truncated frame.
            delivered++;
            const auto elapsed=clockUs()-cycleStart;
            if(elapsed<uint64_t(board::FRAME_PERIOD_MS)*1000)
                vTaskDelay(pdMS_TO_TICKS(board::FRAME_PERIOD_MS-unsigned(elapsed/1000)));
            else vTaskDelay(1);
        }
        client.stop();streaming=false;
    }
}
void serve(WiFiClient client) {
    client.setNoDelay(true);
    char request[1024];size_t length=0;bool complete=false;
    const auto deadline=clockUs()+300000;
    while(client.connected() && length+1<sizeof request && clockUs()<deadline) {
        if(!client.available()){vTaskDelay(1);continue;}
        const int c=client.read();if(c<0)break;request[length++]=char(c);request[length]=0;
        if(length>=4 && !memcmp(request+length-4,"\r\n\r\n",4)){complete=true;break;}
    }
    if(!complete){response(client,"400 Bad Request","text/plain","Incomplete or oversized request\n");return;}
    char* end=strstr(request,"\r\n");if(end)*end=0;
    switch(stream::route(request)) {
    case stream::Route::Stream:
        if(!cameraReady || !streamTask){response(client,"503 Service Unavailable","text/plain","Camera unavailable; check serial log\n");return;}
        if(streaming.exchange(true)){response(client,"409 Conflict","text/plain","One stream client per camera; close the other viewer\n");return;}
        xSemaphoreTake(clientMutex,portMAX_DELAY);pendingClient=client;xSemaphoreGive(clientMutex);
        xTaskNotifyGive(streamTask);return;
    case stream::Route::Sync: {
        const auto now=clockUs();char body[180];
        snprintf(body,sizeof body,"{\"node\":\"%s\",\"boot_id\":\"%08lx\",\"camera_us\":%llu,\"uptime_ms\":%llu}",board::NODE,(unsigned long)bootId,(unsigned long long)now,(unsigned long long)(now/1000));
        response(client,"200 OK","application/json",body);return;
    }
    case stream::Route::Status: {
        char body[640];
        snprintf(body,sizeof body,"{\"node\":\"%s\",\"boot_id\":\"%08lx\",\"camera_ready\":%s,\"camera_error\":%d,\"streaming\":%s,\"ip\":\"192.168.4.%d\",\"rssi_dbm\":%d,\"target_fps\":%d,\"width\":640,\"height\":480,\"jpeg_quality\":%d,\"acquired\":%lu,\"delivered\":%lu,\"stale_dropped\":%lu,\"capture_errors\":%lu,\"send_errors\":%lu,\"free_heap\":%u,\"free_psram\":%u,\"uptime_ms\":%llu}",
            board::NODE,(unsigned long)bootId,cameraReady?"true":"false",int(cameraError),streaming?"true":"false",board::IP_LAST,WiFi.RSSI(),RS_CAMERA_FPS,RS_CAMERA_JPEG_QUALITY,
            (unsigned long)acquired.load(),(unsigned long)delivered.load(),(unsigned long)stale.load(),(unsigned long)captureErrors.load(),(unsigned long)sendErrors.load(),ESP.getFreeHeap(),ESP.getFreePsram(),(unsigned long long)(clockUs()/1000));
        response(client,"200 OK","application/json",body);return;
    }
    case stream::Route::Index:
        response(client,"200 OK","text/html","<!doctype html><meta name=viewport content='width=device-width'><title>RideShield camera</title><h1>RideShield camera</h1><p><a href='/status'>Status</a> | <a href='/sync'>Clock</a></p><p>Close this viewer before opening another stream client.</p><img src='/stream' style='max-width:100%'>");return;
    case stream::Route::MethodNotAllowed:response(client,"405 Method Not Allowed","text/plain","GET required\n");return;
    case stream::Route::BadRequest:response(client,"400 Bad Request","text/plain","Malformed request\n");return;
    default:response(client,"404 Not Found","text/plain","Use /stream, /sync or /status\n");return;
    }
}
}
void setup() {
    Serial.begin(115200);delay(200);pinMode(board::FLASH,OUTPUT);digitalWrite(board::FLASH,LOW);
    bootId=esp_random();Serial.printf("RideShield %s: AI-Thinker ESP32-CAM / OV2640 boot=%08lx\n",board::NODE,(unsigned long)bootId);
    cameraReady=initCamera();
    clientMutex=xSemaphoreCreateMutex();
    if(clientMutex && xTaskCreatePinnedToCore(streamWorker,"camera-stream",6144,nullptr,1,&streamTask,1)!=pdPASS)streamTask=nullptr;
    if(!clientMutex || !streamTask)Serial.println("ERROR: streaming task unavailable");
    if(!strlen(RS_CAMERA_SSID)){Serial.println("CONFIG REQUIRED: copy include/local_config.example.h to local_config.h and set exact controller SSID");return;}
    WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setHostname(board::HOST);WiFi.setSleep(false);WiFi.setAutoReconnect(true);
    networkConfigured=WiFi.config(IPAddress(192,168,4,board::IP_LAST),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
    if(!networkConfigured){Serial.println("ERROR: static IP configuration failed");return;}
    WiFi.begin(RS_CAMERA_SSID,RS_CAMERA_PASSWORD);
}
void loop() {
    static uint32_t retryAt=0,logAt=0;
    const uint32_t now=millis();
    if(networkConfigured && WiFi.status()==WL_CONNECTED) {
        if(!listening){networkEpoch++;server.begin();listening=true;Serial.printf("Wi-Fi connected: http://%s:8080/ (%s)\n",WiFi.localIP().toString().c_str(),board::NODE);}
        WiFiClient client=server.available();if(client)serve(client);
    } else {
        if(listening){networkEpoch++;server.end();listening=false;Serial.println("Wi-Fi lost; stream will close, reconnecting");}
        if(networkConfigured && uint32_t(now-retryAt)>=10000){retryAt=now;WiFi.reconnect();}
    }
    if(uint32_t(now-logAt)>=5000){logAt=now;Serial.printf("%s camera=%d wifi=%d stream=%d sent=%lu stale=%lu capture_err=%lu send_err=%lu heap=%u\n",board::NODE,cameraReady,WiFi.status()==WL_CONNECTED,streaming.load(),(unsigned long)delivered.load(),(unsigned long)stale.load(),(unsigned long)captureErrors.load(),(unsigned long)sendErrors.load(),ESP.getFreeHeap());}
    delay(2);
}
