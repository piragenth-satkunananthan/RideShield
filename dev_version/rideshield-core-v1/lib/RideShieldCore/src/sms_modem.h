#pragma once
#include "core.h"
namespace rs {
class SmsModem{
public:
 enum class Stage:uint8_t{Idle,Attention,Echo,Sim,Registration,TextMode,Charset,Prompt,Submission,Done};
 bool start(const SmsJob&,const char*,Ms);void feed(char,Ms);void tick(Ms);size_t takeTx(uint8_t*,size_t);bool takeResult(SendResult&);
 Stage stage()const{return stage_;}bool busy()const{return stage_!=Stage::Idle&&stage_!=Stage::Done;}
private:
 Stage stage_=Stage::Idle;SmsJob job_{};char number_[17]{},line_[192]{};size_t lineLength_=0,txLength_=0;
 bool overflow_=false,simReady_=false,registered_=false,bodySent_=false,submitted_=false,resultReady_=false;
 uint8_t tx_[192]{};Ms deadline_=0;SendResult result_=SendResult::Failed;
 void command(Stage,const char*,Ms,Ms=3000);void line(Ms);void finish(SendResult);
};
}
