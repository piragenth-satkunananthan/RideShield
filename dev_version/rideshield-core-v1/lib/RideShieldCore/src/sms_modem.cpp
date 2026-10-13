#include "sms_modem.h"
#include <cstdio>
#include <cstring>
namespace rs {
static bool decimal(const char*&p,unsigned&v){while(*p==' ')++p;v=0;unsigned digits=0;while(*p>='0'&&*p<='9'){if(++digits>3)return false;v=v*10+unsigned(*p++-'0');}while(*p==' ')++p;return digits!=0;}
void SmsModem::command(Stage s,const char*cmd,Ms now,Ms timeout){stage_=s;deadline_=now+timeout;txLength_=std::strlen(cmd);std::memcpy(tx_,cmd,txLength_);}
bool SmsModem::start(const SmsJob&job,const char*number,Ms now){
 if(busy()||resultReady_||!validPhoneNumber(number))return false;
 size_t n=0;while(n<sizeof(job.text)&&job.text[n])++n;if(!n||n>160)return false;
 for(size_t i=0;i<n;++i)if(job.text[i]<32||job.text[i]>126||job.text[i]=='^'||job.text[i]=='{'||job.text[i]=='}'||job.text[i]=='['||job.text[i]==']'||job.text[i]=='\\'||job.text[i]=='~'||job.text[i]=='|')return false;
 *this=SmsModem{};job_=job;std::strncpy(number_,number,sizeof(number_)-1);command(Stage::Attention,"AT\r",now);return true;
}
size_t SmsModem::takeTx(uint8_t*out,size_t capacity){if(!out||capacity<txLength_)return 0;size_t n=txLength_;std::memcpy(out,tx_,n);txLength_=0;return n;}
void SmsModem::finish(SendResult r){stage_=Stage::Done;result_=r;resultReady_=true;txLength_=0;}
bool SmsModem::takeResult(SendResult&r){if(!resultReady_)return false;r=result_;resultReady_=false;stage_=Stage::Idle;return true;}
void SmsModem::tick(Ms now){if(busy()&&now>=deadline_)finish(submitted_?SendResult::Submitted:bodySent_?SendResult::Unknown:SendResult::Failed);}
void SmsModem::feed(char ch,Ms now){
 if(!busy())return;
 if(stage_==Stage::Prompt&&ch=='>'){size_t n=std::strlen(job_.text);std::memcpy(tx_,job_.text,n);tx_[n]=0x1a;txLength_=n+1;stage_=Stage::Submission;bodySent_=true;deadline_=now+60000;lineLength_=0;overflow_=false;return;}
 if(ch=='\r')return;
 if(ch=='\n'){if(!overflow_&&lineLength_){line_[lineLength_]=0;line(now);}lineLength_=0;overflow_=false;return;}
 if(lineLength_+1<sizeof(line_)&&!overflow_)line_[lineLength_++]=ch;else overflow_=true;
}
void SmsModem::line(Ms now){
 char*s=line_;while(*s==' ')++s;
 if(!std::strcmp(s,"ERROR")||!std::strncmp(s,"+CMS ERROR:",11)||!std::strncmp(s,"+CME ERROR:",11)){finish(submitted_?SendResult::Submitted:SendResult::Failed);return;}
 if(stage_==Stage::Sim&&!std::strcmp(s,"+CPIN: READY"))simReady_=true;
 if(stage_==Stage::Registration&&!std::strncmp(s,"+CEREG:",7)){unsigned n=99,status=99;const char*p=s+7;if(decimal(p,n)&&*p++==','&&decimal(p,status)&&n<=5&&(*p==0||*p==','))registered_=status==1||status==5;}
 if(stage_==Stage::Submission&&!std::strncmp(s,"+CMGS:",6)){unsigned ref=9999;const char*p=s+6;if(decimal(p,ref)&&!*p&&ref<=255)submitted_=true;}
 if(std::strcmp(s,"OK"))return;
 switch(stage_){
 case Stage::Attention:command(Stage::Echo,"ATE0\r",now);break;
 case Stage::Echo:command(Stage::Sim,"AT+CPIN?\r",now);break;
 case Stage::Sim:if(!simReady_)finish(SendResult::Failed);else command(Stage::Registration,"AT+CEREG?\r",now);break;
 case Stage::Registration:if(!registered_)finish(SendResult::Failed);else command(Stage::TextMode,"AT+CMGF=1\r",now);break;
 case Stage::TextMode:command(Stage::Charset,"AT+CSCS=\"GSM\"\r",now);break;
 case Stage::Charset:{char cmd[48];std::snprintf(cmd,sizeof(cmd),"AT+CMGS=\"%s\"\r",number_);command(Stage::Prompt,cmd,now,10000);break;}
 case Stage::Submission:if(submitted_)finish(SendResult::Submitted);break;
 default:break;}
}
}
