 //for documentation see project at https://github.com/Dilbert66/esphome-vistaecp
#include "vistaalarm.h"
#include "esphome/core/helpers.h"

#if defined(ESP32) && defined(USETASK)
#include <esp_chip_info.h>
#include <esp_task_wdt.h>
#endif

#define KP_ADDR 17 //only used as a default if not set in the yaml
#define MAX_ZONES 48
#define MAX_PARTITIONS 3  
#define DEFAULTPARTITION 1

//default pins to use for serial comms to the panel
//The pinouts below are only examples. You can choose any other gpio pin that is available and not needed for boot.
//These have proven to work fine.
#ifdef ESP32
//esp32 use gpio pins 4,13,16-39 
#define RX_PIN 22
#define TX_PIN 21
#define MONITOR_PIN 18 // pin used to monitor the green TX line (3.3 level dropped from 12 volts
#else
#define RX_PIN 5
#define TX_PIN 4
#define MONITOR_PIN 14 // pin used to monitor the green TX line (3.3 level dropped from 12 volts
#endif

Vista vista;
void disconnectVista() {
  vista.stop();
}
 
#if !defined(ARDUINO_MQTT)
#include "esphome.h"
namespace esphome {
namespace alarm_panel {
    
const char zoneRequest_INIT[]={00,0x68,0x62,0x31,0x45,0x49,0xF5,0x31,0xFB,0x45,0x4A,0xF5,0x32,0xFB,0x45,0x43,0xF5,0x31,0xFB,0x43,0x6C};

static const char *const TAG = "vista_alarm"; 
 
vistaECPHome* alarmPanelPtr; 
#if defined(ESPHOME_MQTT)
std::function<void(const std::string &, JsonObject)> mqtt_callback;
#endif  

void vistaECPHome::add_binary_sensor(binary_sensor::BinarySensor * b,const char * type_id) {

    struct binarySensor bs;
    bs.ptr=b;
    bs.type_id=type_id;
    bMap.push_back(bs);
    ESP_LOGD(TAG,"Added binary sensor %s with type %s",bs.ptr->get_name().c_str(),bs.type_id);

}

void vistaECPHome::add_text_sensor(text_sensor::TextSensor* b,const char * type_id) {
    struct textSensor ts;
    ts.ptr=b;
    ts.type_id=type_id;
    tMap.push_back(ts);
    ESP_LOGD(TAG,"Added text sensor %s with type %s",ts.ptr->get_name().c_str(),ts.type_id);

}


const char * vistaECPHome::getTypeIdFromBinaryObjectId(const std::string & objid) {
    
      auto it = std::find_if(bMap.begin(), bMap.end(),  [objid](struct binarySensor bs){
          
            return  bs.ptr->get_object_id()==objid; 
   } );
   if (it != bMap.end()) 
       return (*it).type_id;
   else
       return "";
}

const char * vistaECPHome::getTypeIdFromTextObjectId(const std::string & objid) {
    
      auto it = std::find_if(tMap.begin(), tMap.end(),  [objid](struct textSensor ts){
          
            return  ts.ptr->get_object_id()==objid; 
   } );
   if (it != tMap.end()) 
       return (*it).type_id;
   else
       return "";
}

void vistaECPHome::publishBinaryState(const std::string& cstr,uint8_t partition,bool open) {
   const char * c;
   if (partition)
       c=(cstr + std::to_string(partition)).c_str();
   else
       c=cstr.c_str();
    auto it = std::find_if(bMap.begin(), bMap.end(),  [c](struct binarySensor bs){
            return strcmp(bs.type_id,c)==0; 
   } );    
   if (it != bMap.end()) (*it).ptr->publish_state(open);
}
    
void vistaECPHome::publishTextState(const std::string & cstr,uint8_t partition,std::string * text) {
   const char * c;
   if (partition)
       c=(cstr + std::to_string(partition)).c_str();
   else
       c=cstr.c_str();
  auto it = std::find_if(tMap.begin(), tMap.end(),  [c](struct textSensor ts){ 
    return strcmp(ts.type_id,c)==0; 
  } 
  );
  if (it != tMap.end()) (*it).ptr->publish_state(*text);  
  
}


#endif

void vistaECPHome::stop() {
#if defined(ESP32) &&  defined(USETASK)   
if (xHandle != NULL)    
    vTaskSuspend( xHandle );
#endif
vista.stop();
}    

 vistaECPHome::vistaECPHome(char kpaddr, int receivePin, int transmitPin, int monitorTxPin,int maxzones,int maxpartitions,bool invertrx,bool inverttx, bool invertmon,uint8_t inputrx, uint8_t inputmon): 
    keypadAddr1(kpaddr),
    rxPin(receivePin),
    txPin(transmitPin),
    monitorPin(monitorTxPin),
    maxZones(maxzones),
    maxPartitions(maxpartitions),    
    invertRx(invertrx),
    invertTx(inverttx),
    invertMon(invertmon),
    inputRx(inputrx),
    inputMon(inputmon)
    {
         partitionKeypads = new char[maxPartitions+1];
         partitions = new uint8_t[maxPartitions];
         partitionStates = new partitionStateType[maxPartitions];
         alarmPanelPtr=this;         
#if defined(ESPHOME_MQTT) 
      mqtt_callback=on_json_message; 
#endif           
    }
    
    void vistaECPHome::zoneStatusUpdate(zoneType* zt) {
      if (zoneStatusChangeCallback != NULL ) {
          std::string msg,zs1;   
          zs1=zt->check?"T":zt->open?"O":"C";
          msg = zt->bypass ? "B" : zt->alarm ? "A" : "";
          zoneStatusChangeCallback(zt->zone,msg.append(zs1).c_str());
      }
      
      if (zoneStatusChangeBinaryCallback != NULL ) {
        if (zt->zone <= maxZones) 
              zoneStatusChangeBinaryCallback(zt->zone, zt->open ||  zt->check );
         else
            zoneStatusChangeBinaryCallback(zt->zone,zt->check || zt->open || zt->alarm || zt->trouble);
      }
    }
 /*   
bool vistaECPHome::zoneActive(uint16_t zone) {

  std::string str = "z" + std::to_string(zone) ;
  auto itb = std::find_if(bMap.begin(), bMap.end(),  [&str](binary_sensor::BinarySensor* f){ return f->get_type_id() == str; } );
  if (itb != bMap.end()) return true;
  
  auto itt = std::find_if(tMap.begin(), tMap.end(),  [&str](text_sensor::TextSensor* f){ return f->get_type_id() == str; } );
  if (itt != tMap.end()) return true; 
  
  return false;

}   
*/

#if !defined(ARDUINO_MQTT)
void vistaECPHome::loadZones() {

    for (struct binarySensor obj : bMap ) {
    std::string id=obj.type_id;
    const std::regex e("^[zZ]([0-9]+)$");
    std::smatch m;
    if (std::regex_search(id,m,e)) {
        int z = toInt(m[1],10);        
        createZone(z);
    }
 }
 
 for (struct textSensor obj : tMap ) {
       
    std::string id=obj.type_id;
    const std::regex e("^[zZ]([0-9]+)$");
    std::smatch m;
    if (std::regex_search(id,m,e)) {
        int z = toInt(m[1],10);        
        createZone(z);
    }
 }
}
#endif
vistaECPHome::zoneType * vistaECPHome::createZone(uint16_t z) {

     zoneType n = zonetype_INIT; 
     
     n.zone=z;
     n.active=true;
     
     ESP_LOGD(TAG,PSTR("adding zone %d"),z);
     extZones.push_back(n);
     return &extZones.back();     
}

vistaECPHome::zoneType * vistaECPHome::getZone(uint16_t z) {

     auto it = std::find_if(extZones.begin(), extZones.end(),  [&z](zoneType& f){ return f.zone == z; } );
     if (it != extZones.end()) return &(*it);
     #if defined(ARDUINO_MQTT)
     return createZone(z);
     #else     
     return &zonetype_INIT;
     #endif

}

vistaECPHome::serialType vistaECPHome::getRfSerialLookup(char * serialCode) { 

  serialType rf;
  rf.zone=0;
  if (rfSerialLookup && *rfSerialLookup) {
    std::string serial=serialCode;      
     
    std::string s = rfSerialLookup;

    size_t pos, pos1,pos2;
    s.append(",");
    while ((pos = s.find(',')) != std::string::npos) {
      std::string token,token1, token2, token3; 
      token = s.substr(0, pos); 
      pos1 = token.find(':');
      pos2=token.find(':',pos1+1); 
      token1 = token.substr(0, pos1); //serial
      if (pos2 != std::string::npos)  {     
        token2 = token.substr(pos1 + 1,pos2-pos1-1); //loop
        token3 = token.substr(pos2+1);    //zone
      }
      if (token1 == serial && token2!="" && token3 !="") {
        rf.zone=toInt(token3,10);
        uint8_t loop=toInt(token2,10);
        switch (loop) {
          case 1: rf.mask=0x80;break;
          case 2: rf.mask=0x40;break;
          case 3: rf.mask=0x20;break;
          case 4: rf.mask=0x10;break;
          default: rf.mask=0x80;break;
        }

        break;
      }
      s.erase(0, pos + 1); /* erase() function store the current positon and move to next token. */
    }
  }
  return rf;
}   



#if defined(ESPHOME_MQTT) 

void vistaECPHome::on_json_message(const std::string &topic, JsonObject payload) {
    int p=0;
      
      if (topic.find(String(FPSTR(setalarmcommandtopic)).c_str())!=std::string::npos) { 
        if (payload.containsKey("partition"))
          p=payload["partition"];
      
        if (payload.containsKey("addr")) {
            p=payload["addr"];            
            std::string s=payload["keys"];
        int NumberChars = s.length();
        char * bytes=new char[NumberChars/2];
        for (int i = 0; i < NumberChars; i += 2)
        {
             bytes[i / 2] = alarmPanelPtr->toInt(s.substr(i, 2), 16);
        }
            vista.writeDirect(bytes,p,NumberChars/2);
            return;
        }
        if (payload.containsKey("state") )  {
            const char *c="";             
            if (payload.containsKey("code"))
                c=payload["code"];
            std::string code=c;
            std::string s=payload["state"];  
            alarmPanelPtr->set_alarm_state(s,code,p); 
        } else if (payload.containsKey("keys")) {
            std::string s=payload["keys"]; 
             alarmPanelPtr->alarm_keypress_partition(s,p);
        } else if (payload.containsKey("fault") && payload.containsKey("zone")) {
            bool b=false;
            std::string s1 =  payload["fault"];
            if (s1=="ON" || s1=="on" || s1=="1")
                b=true;
             p=payload["zone"];
           // ESP_LOGE(TAG,"set zone fault %s,%s,%d,%d",s2.c_str(),c,b,p);            
             alarmPanelPtr->set_zone_fault(p,b);

        }
        
      } 
      
  }
#endif

 void vistaECPHome::set_panel_time() {
#if defined(USE_TIME)      
    if (vistaCmd.statusFlags.programMode) return;
    ESPTime rtc = now();
    if (!rtc.is_valid()) return;
    int hour=rtc.hour;
    int year=rtc.year;
    char ampm=hour<12?2:1;
    if (hour > 12) hour-=12;
    char cmd[30];
    sprintf(cmd,PSTR("%s#63*%02d%02d%1d%02d%02d%02d*"),accessCode,hour,rtc.minute,ampm,rtc.year%100,rtc.month,rtc.day_of_month);
    #if not defined(ARDUINO_MQTT)    
    ESP_LOGD(TAG,PSTR("Send time string: %s"),cmd);
    #endif
    int addr=partitionKeypads[defaultPartition]; 
    vista.write(cmd,addr);
#endif    
  }

  void vistaECPHome::set_panel_time_manual(int year,int month,int day,int hour,int minute) {
    if (vistaCmd.statusFlags.programMode) return;
    char ampm=hour<12?2:1;
    if (hour > 12) hour-=12;
    char cmd[30];
    sprintf(cmd,PSTR("%s#63*%02d%02d%1d%02d%02d%02d*"),accessCode,hour,minute,ampm,year%100,month,day);
    #if defined(ARDUINO_MQTT)
        Serial.printf("Setting panel time...\n");      
    #else
       ESP_LOGD(TAG,PSTR("Send time string: %s"),cmd);
    #endif

    int addr=partitionKeypads[defaultPartition]; 
    vista.write(cmd,addr);    
  } 

#if defined(ARDUINO_MQTT)
void vistaECPHome::begin() {
#else
void vistaECPHome::setup()  {
#endif 
      //use a pollingcomponent and change the default polling interval from 16ms to 8ms to enable
      // the system to not miss a response window on commands.  
#if !defined(ARDUINO_MQTT)     
  set_update_interval(8); //set looptime to 8ms 
  loadZones();
#endif     
#if defined(ESPHOME_MQTT)
   topic_prefix =mqtt::global_mqtt_client->get_topic_prefix();
   mqtt::MQTTDiscoveryInfo mqttDiscInfo=mqtt::global_mqtt_client->get_discovery_info();
   std::string discovery_prefix=mqttDiscInfo.prefix;
   topic=discovery_prefix+PSTR("/alarm_control_panel/")+ topic_prefix + PSTR("/config"); 
   mqtt::global_mqtt_client->subscribe_json(topic_prefix + String(FPSTR(setalarmcommandtopic)).c_str(),mqtt_callback);     
#endif  
#if  defined(USE_API)
      register_service( & vistaECPHome::set_panel_time, "set_panel_time", {});
      register_service( & vistaECPHome::alarm_keypress, "alarm_keypress", {
        "keys"
      });
      //register_service( & vistaECPHome::send_cmd_bytes, "send_cmd_bytes", {
     //   "addr","hexdata"
    // });      
      register_service( & vistaECPHome::alarm_keypress_partition, "alarm_keypress_partition", {
        "keys","partition"
      });      
      register_service( & vistaECPHome::alarm_disarm, "alarm_disarm", {
        "code","partition"
      });
      register_service( & vistaECPHome::alarm_arm_home, "alarm_arm_home",{"partition"});
      register_service( & vistaECPHome::alarm_arm_night, "alarm_arm_night",{"partition"});
      register_service( & vistaECPHome::alarm_arm_away, "alarm_arm_away",{"partition"});
      register_service( & vistaECPHome::alarm_trigger_panic, "alarm_trigger_panic", {
        "code","partition"
      });
      register_service( & vistaECPHome::alarm_trigger_fire, "alarm_trigger_fire", {
        "code","partition"
      });
      register_service( & vistaECPHome::set_zone_fault, "set_zone_fault", {
        "zone",
        "fault"
      });
      
#endif     
      systemStatusChangeCallback(STATUS_ONLINE, 1);
      statusChangeCallback(sac, true, 1);
      vista.begin(rxPin, txPin, keypadAddr1, monitorPin,invertRx,invertTx,invertMon,inputRx,inputMon);

      if (zoneStatusChangeBinaryCallback != NULL) {
        for (int x = 1; x <= maxZones; x++) {
            yield();
            zoneStatusChangeBinaryCallback(x,false);
            zoneStatusChangeCallback(x,"C");
        }
      }
   
      firstRun = true;

      vista.lrrSupervisor = lrrSupervisor; //if we don't have a monitoring lrr supervisor we emulate one if set to true
      //set addresses of expander emulators
      for (int x=0;x<9;x++) {
          vista.zoneExpanders[x].expansionAddr=expanderAddr[x];
      }
            
      setDefaultKpAddr(defaultPartition);
      
      for (uint8_t p=0;p < maxPartitions;p++) { 
        partitions[p]=0;  
        systemStatusChangeCallback(STATUS_NOT_READY,p+1);
        beepsCallback("0",p+1);
      }    
      lrrMsgChangeCallback(PSTR("ESP Restart"));
      rfMsgChangeCallback(""); 
      
#if defined(ESP32) && defined(USETASK)   
  esp_chip_info_t info;
  esp_chip_info(&info);
  ESP_LOGD(TAG,"Cores: %d,arduino core=%d",info.cores,CONFIG_ARDUINO_RUNNING_CORE);
  uint8_t core= info.cores>1?ASYNC_CORE:0;
    xTaskCreatePinnedToCore(
    this -> cmdQueueTask, //Function to implement the task
    "cmdQueueTask", //Name of the task
    3200, //Stack size in words
    (void * ) this, //Task input parameter
    10, //Priority of the task
    &xHandle //Task handle.
    ,core //Core where the task should run
  ); 
#endif  
     
    }

    void vistaECPHome::alarm_disarm(std::string code,int partition) {

      set_alarm_state("D", code,partition);

    }

    void vistaECPHome::alarm_arm_home(int partition) {

      set_alarm_state("S","",partition);

    }

    void vistaECPHome::alarm_arm_night(int partition) {

      set_alarm_state("N","",partition);

    }

    void vistaECPHome::alarm_arm_away(int partition) {

      set_alarm_state("A","",partition);

    }

    void vistaECPHome::alarm_trigger_fire(std::string code,int partition) {

      set_alarm_state("F", code,partition);

    }

    void vistaECPHome::alarm_trigger_panic(std::string code,int partition) {

      set_alarm_state("P", code,partition);

    }

    void vistaECPHome::set_zone_fault(int zone, bool fault) {

      vista.setExpFault(zone, fault);

    }

    void vistaECPHome::alarm_keypress(std::string keystring) {
         
        alarm_keypress_partition(keystring,defaultPartition);
    }    

    void vistaECPHome::alarm_keypress_partition(std::string keystring, int partition) {
      if (keystring=="R") {
            forceRefreshGlobal=true;
            forceRefresh=true;
            return;
      }
      if(keystring=="A") {set_alarm_state("A","",partition);return;}
      if(keystring=="S") {set_alarm_state("S","",partition);return;}
      if(keystring=="N") {set_alarm_state("N","",partition);return;}
      if(keystring=="I") {set_alarm_state("I","",partition);return;}     
      if(keystring=="B") {set_alarm_state("B","",partition);return;}   
      if(keystring=="Y") {set_alarm_state("Y","",partition);return;}     
      
      if (!partition) partition = defaultPartition;      
      if (debug > 0)
          #if defined(ARDUINO_MQTT)
          Serial.printf("Writing keys: %s to partition %d\n", keystring.c_str(),partition);     
          #else
          ESP_LOGD(TAG, PSTR("Writing keys: %s to partition %d"), keystring.c_str(),partition);
          #endif
      uint8_t addr=0;
      if (partition > maxPartitions || partition < 1) return;
      addr=partitionKeypads[partition];
      if (addr > 0 and addr < 24)      
        vista.write(keystring.c_str(),addr);
    }
    
    void vistaECPHome::send_cmd_bytes(int addr,std::string hexbytes) {
        int NumberChars = hexbytes.length();
        char * bytes=new char[NumberChars/2];
        for (int i = 0; i < NumberChars; i += 2)
        {
             bytes[i / 2] = toInt(hexbytes.substr(i, 2), 16);
        }
            vista.writeDirect(bytes,addr,NumberChars/2);
            
        return;
        
    }
    
    void vistaECPHome::setDefaultKpAddr(uint8_t p) {
    uint8_t a;
      if (p > maxPartitions || p < 1) return;
      a=partitionKeypads[p];
      if (a > 15 && a < 24) 
        vista.setKpAddr(a);
    } 

    bool vistaECPHome::isInt(std::string s, int base) {
      if (s.empty() || std::isspace(s[0])) return false;
      char * p;
      strtol(s.c_str(), & p, base);
      return ( * p == 0);
    }

    long int vistaECPHome::toInt(std::string s, int base) {
      if (s.empty() || std::isspace(s[0])) return 0;
      char * p;
      long int li = strtol(s.c_str(), & p, base);
      return li;
    }
    
 

    bool vistaECPHome::areEqual(char * a1, char * a2, uint8_t len) {
      for (int x = 0; x < len; x++) {
        if (a1[x] != a2[x]) return false;
      }
      return true;
    }
    
 std::string vistaECPHome::getNameFromPrompt(char *p1, char *p2) {
    if (vistaCmd.cbuf[0] != 0xf7) return "";
   std::string p=std::string(p1) + std::string(p2);
   const std::regex e("[a-zA-Z]+\\s+([0-9]+)\\s+(.*?)\\s*$");
   const std::regex r("\\s+");
   std::smatch m;
   if (std::regex_search(p,m,e)) {
      std::string m1=m[1];
      std::string m2=m[2];
      m2=std::regex_replace(m2,r," ");
      ESP_LOGD(TAG,PSTR("name match=%s,zone=%s"),m2.c_str(),m1.c_str());
      return m2;
   }
             
   return "";  
}   

 int vistaECPHome::getZoneFromPrompt(char *p1) {
   if (vistaCmd.cbuf[0] != 0xf7) return 0;
   std::string p=std::string(p1);
   const std::regex e("[a-zA-Z]+\\s+([0-9]+)\\s+(.*?)\\s*$");
   std::smatch m;
   if (std::regex_search(p,m,e)) {
     int z = toInt(m[1],10);
     vistaCmd.statusFlags.zone=z;
     ESP_LOGD(TAG,"zone match=%d",z);
     return z;
   }
             
   return 0;  
} 

  void vistaECPHome::printPacket(const char * label, char cbuf[], int len) {
    char s1[4];

    std::string s="";      
      
    #if !defined(ARDUINO_MQTT) 
    char s2[25];    
    ESPTime rtc=now();
    sprintf(s2,PSTR("%02d-%02d-%02d %02d:%02d "),rtc.year,rtc.month,rtc.day_of_month,rtc.hour,rtc.minute);
    #endif
    for (int c = 0; c < len; c++) {
      sprintf(s1, "%02X ", cbuf[c]);
      s=s.append(s1);
      yield();
    }
    #if defined(ARDUINO_MQTT)
    Serial.printf("%s: %s\n",label, s.c_str());    
    #else 
    ESP_LOGI(label, PSTR("%s %s"),s2, s.c_str());
    #endif

  }

  /* 
    std::string vistaECPHome::getF7Lookup(char cbuf[]) {

        std::string s="{";
        char s1[10];
        for (int c = 12; c < 17; c++) {
            sprintf(s1, "%d,", cbuf[c]);
            s.append(s1);
        }
        s.append("0}");
        return s;

    }  
*/
    void vistaECPHome::set_alarm_state(std::string const& state, std::string code,int partition) {

      if (code.length() != 4 || !isInt(code, 10)) code = accessCode; // ensure we get a numeric 4 digit code
 
      uint8_t addr=0;
      if (partition > maxPartitions || partition < 1) return;
      addr=partitionKeypads[partition];
      if (addr < 1 || addr > 23) return;          

      // Arm stay
      if (state.compare("S") == 0 && !partitionStates[partition-1].previousLightState.armed) {
        if (quickArm )
          vista.write("#3",addr);
        else if (code.length() == 4) {
          vista.write(code.c_str(),addr);
          vista.write("3",addr);
        }
      }
      // Arm away
      else if ((state.compare("A") == 0 || state.compare("W") == 0) && !partitionStates[partition-1].previousLightState.armed) {

        if (quickArm)
          vista.write("#2",addr);
        else if (code.length() == 4) {
          vista.write(code.c_str(),addr);
          vista.write("2",addr);
        }
      }
      else if (state.compare("I") == 0 && !partitionStates[partition-1].previousLightState.armed) {
        if (quickArm)
          vista.write("#7",addr);
        else if (code.length() == 4) {
          vista.write(code.c_str(),addr);
          vista.write("7",addr);
        }

      }      
      else if (state.compare("N") == 0 && !partitionStates[partition-1].previousLightState.armed) {

        if (quickArm)
          vista.write("#33",addr);
        else if (code.length() == 4) {
          vista.write(code.c_str(),addr);
          vista.write("33",addr);
        }
      }
      // Fire command
      else if (state.compare("F") == 0) {

        //todo

      }
      // Panic command
      else if (state.compare("P") == 0) {

        //todo
      } else if (state.compare("B") == 0) {
        if (code.length() == 4) {
          vista.write(code.c_str(),addr);
          vista.write("6#",addr);
        }
      } else if (state.compare("Y") == 0) {
        if (code.length() == 4) {
          vista.write(code.c_str(),addr);
          vista.write("600",addr);
        }
      } 
      else if (state.compare("D") == 0) {
        if (code.length() == 4) { // ensure we get 4 digit code
          vista.write(code.c_str(),addr);
          vista.write("1",addr);
          vista.write(code.c_str(),addr);
          vista.write("1",addr);
        }
      }
    }

    int vistaECPHome::getZoneFromChannel(uint8_t deviceAddress, uint8_t channel) {

      switch (deviceAddress) {
      case 7:
        return channel + 8;
      case 8:
        return channel + 16;
      case 9:
        return channel + 24;
      case 10:
        return channel + 32;
      case 11:
        return channel + 40;
      default:
        return 0;
      }

    }

    void vistaECPHome::assignPartitionToZone(zoneType * zt) {
        
        for (int p=1;p<4;p++) {
            if (partitions[p-1]) {
                ESP_LOGD(TAG,PSTR("Assigning partition %d, to zone %d"),p,zt->zone);
                zt->partition=p;
                break;
            }
            
        }
    }


    void vistaECPHome::getPartitionsFromMask() {
      partitionTargets=0;
      memset(partitions, 0,maxPartitions);
        for (uint8_t p=1;p <= maxPartitions;p++) {
            for (int8_t i=3;i>=0;i--) {
                int8_t shift=partitionKeypads[p]-(8*i);
                if (shift >= 0 && (vistaCmd.statusFlags.keypad[i] & (0x01 << shift))) {
                    partitionTargets=partitionTargets+1;
                    partitions[p-1] = 1;
                    break;
                }
            }
      }
    }


void vistaECPHome::sendZoneRequest(uint8_t partition,reqStates request) {
     if (!auiAddr || !(request==sopenzones || request==sbypasszones)) return;
     char bytes[]={00,0x68,0x62,0x31,0x45,0x49,0xF5,0x31,0xFB,0x45,0x4A,0xF5,0x32,0xFB,0x45,0x43,0xF5,0x31,0xFB,0x43,0x6C};
     bytes[7]=partition;
     bytes[12]=request==sopenzones?0x32:0x35;
     ESP_LOGD(TAG,PSTR("Sending zone status request %d"),request);
     vista.writeDirect(bytes, auiAddr,sizeof(bytes));
}

char * vistaECPHome::parseAUIMessage(char * cmd,reqStates request) {
    
    cmd[cmd[1]+1]=0;// 0 to terminate cmd to use as string
    char * c=&cmd[8]; //advance to start of fe xx byte
    char * f=NULL;
    if (request == sopenzones || request==sbypasszones) {
        char s[]={0xfe,0xfe,0xfe,0xfe,0xec,0};
        f=strstr(c,s);
        if (f)
         return f+strlen(s);        
    }
    

    return NULL;
    

}
void vistaECPHome::updateZoneState(zoneType * zt,int p,reqStates r,bool state,unsigned long t) {
    
          ESP_LOGD(TAG,PSTR("Setting zone %d=%d, partition %d"),zt->zone,state,p);                
          if (r==sopenzones) {
                  zt->open=state;                  
                  zoneStatusUpdate(zt);
         } else if (r==sbypasszones) {
                zt->bypass=state; 
                zoneStatusUpdate(zt);
         }                
         zt->time=t;
         zt->partition=p;
}
   
 void vistaECPHome::processZoneList(uint8_t partition,reqStates request, char * list) {
    if (!list) return;
    for (uint8_t x=0;x<sizeof(list);x++) 
        list[x]=!list[x]?',':list[x];
    std::string zs=list;
    std::smatch sm{}; 

    ESP_LOGD(TAG,PSTR("List=%s"),zs.c_str());
    uint8_t p=partition - 0x30; // set 0x31 - 0x34 to 1 - 4 range

    const std::regex re{ R"(((\d+)-(\d+))|(\d+))" }; //search for ranges
    
    // Search all occureences of integers OR ranges
    unsigned long t=millis();
    for (std::string s{ zs }; std::regex_search(s, sm, re); s = sm.suffix()) {
        // We found something. Was it a range?

        if (sm[1].str().length()) 
            // Yes, range, add all values within to the vector  
            for (int z{ std::stoi(sm[2]) }; z <= std::stoi(sm[3]); ++z) {
                updateZoneState(getZone(z),p,request,true,t);
            }
        else {
            // No, no range, just a plain integer value. Add it to the vector
            int z=std::stoi(sm[0]);
            updateZoneState(getZone(z),p,request,true,t);
        }
        
    }
    //clear  bypass/open zones for partition p that were not set above
     auto it = std::find_if(extZones.begin(), extZones.end(),  [&p,&t](zoneType& f){ return (f.partition == p && f.active && f.time != t && (f.open || f.bypass)); } );

     while (it != extZones.end()) {
            updateZoneState(&(*it),p,request,false,0);
            it = std::find_if(++it, extZones.end(),  [&p,&t](zoneType& f){ return (f.partition == p && f.active  && f.time != t && (f.open || f.bypass)); } );
       }
    forceRefreshZones=true;
 }
 
#if defined(ESP32) && defined(USETASK)

void vistaECPHome::cmdQueueTask(void * args) {

  vistaECPHome * _this = (vistaECPHome * ) args;
  static unsigned long checkTime = millis();  
  for (;;) { 
        const TickType_t xDelay = 2 / portTICK_PERIOD_MS;
        if ( !vista.handle() )
              vTaskDelay(xDelay);
        else
            taskYIELD();
        #if not defined(ARDUINO_MQTT)   
        if (millis() - checkTime > 30000) {
         UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
         ESP_LOGD(TAG,PSTR("High water stack level: %5d"), (uint16_t) uxHighWaterMark);
         checkTime=millis();
        }
        #endif
        
  }
  vTaskDelete(NULL);
}
#endif


#if defined(ARDUINO_MQTT)
void vistaECPHome::loop()  {
#else   
void vistaECPHome::update()  {
#endif 
        
      #if defined(ESPHOME_MQTT)
        if (firstRun && mqtt::global_mqtt_client->is_connected()) {
         mqtt::global_mqtt_client->publish(topic,PSTR("{\"name\":\"command\", \"cmd_t\":\"") +  topic_prefix + String(FPSTR(setalarmcommandtopic)).c_str() + PSTR("\"}"),0,1);
        }
      #endif   

      //if data to be sent, we ensure we process it quickly to avoid delays with the F6 cmd

      

#if !defined(ESP32) or  !defined(USETASK)  
      vista.handle();
      static unsigned long sendWaitTime = millis();
      while (!firstRun && vista.keybusConnected && vista.sendPending() && vista.cmdAvail()) {
        vista.handle();
        if (millis() - sendWaitTime > 10) break;
      }
#endif
      

       
       // if (!vista.cmdQueue.empty()) {
      //   vistaCmd=vista.cmdQueue.front();
       //  vista.cmdQueue.pop();
       if (vista.cmdAvail()) {
         vistaCmd=vista.getNextCmd();
         /*
       static unsigned long refreshFlagsTime;
       if (((!firstRun && vista.keybusConnected && millis() - refreshFlagsTime > 60000 ) || forceRefreshGlobal )&&  !vistaCmd.statusFlags.programMode) {
              forceRefreshZones=true;
              forceRefreshGlobal=false;
              refreshFlagsTime=millis();
              for (uint8_t partition = 1; partition <= maxPartitions; partition++) {
                   partitionStates[partition-1].refreshStatus=true;
                   partitionStates[partition-1].refreshLights=true;
              }
            #if not defined(ARDUINO_MQTT)              
             ESP_LOGD(TAG,"Force refresh....");
            #endif
           
      }   
*/      
        if (firstRun) {
             forceRefreshZones=true;
              forceRefreshGlobal=false;
              for (uint8_t partition = 1; partition <= maxPartitions; partition++) {
                   partitionStates[partition-1].refreshStatus=true;
                   partitionStates[partition-1].refreshLights=true;
              }
            
        }
          /*
          //rf testing code
          static unsigned long testtime=millis();
          static char t1=0;
          if (!vistaCmd.newExtCmd && millis() - testtime > 10000) {
              //FB 04 06 18 98 B0 00 00 00 00 00 00 
              //0399512 b0
              vistaCmd.newExtCmd=true;
              vistaCmd.extcmd[0]=0xfb;
              vistaCmd.extcmd[1]=4;
              vistaCmd.extcmd[2]=6;
              vistaCmd.extcmd[3]=0x18;
              vistaCmd.extcmd[4]=0x98;
              vistaCmd.extcmd[5]=0xb0;
              if (t1==1) {
                  t1=2;
                  vistaCmd.extcmd[5]=0;
              } else 
              if (t1==2) {
                  t1=0;
                  vistaCmd.extcmd[5]=0xb2;
              } else
              if (t1==0) t1=1;
              testtime=millis();
          }
         */
        if (!vistaCmd.newExtCmd && !vistaCmd.newCmd && debug > 0) {
                              if (vistaCmd.cbuf[0]==0xF7)
                       printPacket("CHK", vistaCmd.cbuf, 45);
                   else
             printPacket("CHK", vistaCmd.cbuf, 13);
             return;
        }
         
        static unsigned long refreshLrrTime,refreshRfTime;
        //process ext messages for zones
        if (vistaCmd.newExtCmd) {
          if (debug > 0) {
            if (vistaCmd.extcmd[0]==0xF6)
                printPacket("EXT", vistaCmd.extcmd, vistaCmd.extcmd[3]+4);    
               else            
                printPacket("EXT", vistaCmd.extcmd, 13);
          //format: [0xFA] [deviceid] [subcommand] [channel/zone] [on/off] [relaydata]
          }
          if (vistaCmd.extcmd[0] == 0xFA) {
            int z = vistaCmd.extcmd[3];
            if (vistaCmd.extcmd[2] == 0xf1 && z > 0 && z <= maxZones) { // we have a zone status (zone expander address range)
                ESP_LOGD(TAG,"fa status");
                zoneType * zt=getZone(z);
                
                if (zt->active) {
                    zt->time = millis();
                    zt->open = vistaCmd.extcmd[4];
                    zoneStatusUpdate(zt);
                }
                
                
            } else if (vistaCmd.extcmd[2] == 0x00) { //relay update z = 1 to 4
              if (z > 0) {
                relayStatusChangeCallback(vistaCmd.extcmd[1], z, vistaCmd.extcmd[4] ? true : false);
                if (debug > 0)
          #if defined(ARDUINO_MQTT)
                  Serial.printf("Got relay address %d channel %d = %d\n", vistaCmd.extcmd[1], z, vistaCmd.extcmd[4]);      
          #else                    
                  ESP_LOGD(TAG, PSTR("Got relay address %d channel %d = %d"), vistaCmd.extcmd[1], z, vistaCmd.extcmd[4]);
          #endif
              }
            } else if (vistaCmd.extcmd[2] == 0x0d) { //relay update z = 1 to 4 - 1sec on / 1 sec off
              if (z > 0) {
                // relayStatusChangeCallback(vistaCmd.extcmd[1],z,vistaCmd.extcmd[4]?true:false);
                if (debug > 0)
          #if defined(ARDUINO_MQTT)
                 Serial.printf("Got relay address %d channel %d = %d. Cmd 0D. Pulsing 1sec on/ 1sec off\n", vistaCmd.extcmd[1], z, vistaCmd.extcmd[4]);      
          #else                    
                  ESP_LOGD(TAG, PSTR("Got relay address %d channel %d = %d. Cmd 0D. Pulsing 1sec on/ 1sec off"), vistaCmd.extcmd[1], z, vistaCmd.extcmd[4]);
          #endif
              }
            } else if (vistaCmd.extcmd[2] == 0xf7) { //30 second zone expander module status update
              uint8_t faults = vistaCmd.extcmd[4];
              for (int x = 8; x > 0; x--) {
                z = getZoneFromChannel(vistaCmd.extcmd[1], x); //device id=extcmd[1]
                if (!z) continue;
                bool zs = faults & 1 ?true : false; //check first bit . lower bit = channel 8. High bit= channel 1
                faults = faults >> 1; //get next zone status bit from field
                  zoneType * zt=getZone(z); 
                  if (zt->open != zs && zt->active) {
                      zt->open = zs;                      
                      zoneStatusUpdate(zt);
                  }
                  zt->time = millis();
              }
            }
          } else if (vistaCmd.extcmd[0] == 0xFB && vistaCmd.extcmd[1] == 4) {
              
            char rf_serial_char[14];
            char rf_serial_char_out[20];            
            //FB 04 06 18 98 B0 00 00 00 00 00 00 
            uint32_t device_serial = (vistaCmd.extcmd[2] << 16) + (vistaCmd.extcmd[3] << 8) + vistaCmd.extcmd[4];
            sprintf(rf_serial_char, PSTR("%03d%04d"), device_serial / 10000, device_serial % 10000);
            serialType rf=getRfSerialLookup(rf_serial_char);
            int z=rf.zone;            

            if (debug > 0) {
          #if defined(ARDUINO_MQTT)
                Serial.printf("RFX: %s,%02x\n", rf_serial_char,vistaCmd.extcmd[5]);          
          #else                
                ESP_LOGI(TAG, PSTR("RFX: %s,%02x"), rf_serial_char,vistaCmd.extcmd[5]);
          #endif
            }  
            if (z && !(vistaCmd.extcmd[5]&4) && !(vistaCmd.extcmd[5]&1)) { //ignore heartbeat
                zoneType * zt=getZone(z);
                if (zt->active) {                
                    zt->time = millis();
                    zt->open = vistaCmd.extcmd[5]&rf.mask?true:false;
                    zt->lowbat=vistaCmd.extcmd[5]&2?true:false; //low bat
                    zoneStatusUpdate(zt);
                }
              }
            
            sprintf(rf_serial_char_out,PSTR("%s,%02x"),rf_serial_char,vistaCmd.extcmd[5]);
            rfMsgChangeCallback(rf_serial_char);
            refreshRfTime = millis();

          } 
          /* rf_serial_char
          
          	  1 - ? (loop flag?)
              2 - Low battery
              3 -	Supervision required /heartbeat
              4 - ?
              5 -	Loop 3 
              6 -	Loop 2 
              7 -	Loop 4 
              8 -	Loop 1 
          
          */

        }
       static uint8_t partitionRequest=0;// partition 1 = 0x31
       static reqStates reqState=sidle;
 
        if (debug > 0 && vistaCmd.newCmd) {
            if (vistaCmd.cbuf[0]==0xF2)
                printPacket("CMD", vistaCmd.cbuf, vistaCmd.cbuf[1]+2);
              else
                 printPacket("CMD", vistaCmd.cbuf, 13);
        }
        if (vistaCmd.cbuf[0] == 0xF2 && vistaCmd.newCmd && auiAddr)  {
            ESP_LOGD(TAG,PSTR("state = %d"),reqState);
            if (((vistaCmd.cbuf[2]>>1) & auiAddr)  && (vistaCmd.cbuf[7] & 0xf0)==0x60 && vistaCmd.cbuf[8]==0x63 && vistaCmd.cbuf[9]==0x02 && reqState==sidle) { //partition update broadcast
                partitionRequest=vistaCmd.cbuf[13];
                reqState=sopenzones;
                sendZoneRequest(partitionRequest,reqState);
            } else if (((vistaCmd.cbuf[2]>>1) & auiAddr) && (vistaCmd.cbuf[7] & 0xf0)==0x50 && vistaCmd.cbuf[8]==0xfe ) { //response data from request
                  char * m=parseAUIMessage(vistaCmd.cbuf,reqState);
                  
                  if (reqState==sopenzones || reqState==sbypasszones) {
                    processZoneList(partitionRequest,reqState,m);
                    reqState=reqState==sbypasszones?reqState=sidle:reqState=sbypasszones;
                    if (reqState==sbypasszones)
                        sendZoneRequest(partitionRequest,reqState);
                  }
                  
                  
                }  else reqState==sidle;
            return;
        } else if (vistaCmd.cbuf[0] == 0xf7 && vistaCmd.newCmd) {
          getPartitionsFromMask();
          for (uint8_t partition = 1; partition <= maxPartitions; partition++) {
            if (partitions[partition - 1] ) {
               forceRefresh=partitionStates[partition - 1].refreshStatus ;
        
          #if defined(ARDUINO_MQTT)
              Serial.printf("Partition: %02X\n", partition);          
          #else              
              ESP_LOGI(TAG, PSTR("Partition: %02X"), partition);
          #endif
          
                line1DisplayCallback(vistaCmd.statusFlags.prompt1, partition);
                line2DisplayCallback(vistaCmd.statusFlags.prompt2, partition);
              if (partitionStates[partition - 1].lastbeeps != vistaCmd.statusFlags.beeps || forceRefresh ) {
                beepsCallback(std::to_string( vistaCmd.statusFlags.beeps), partition);
              }

              partitionStates[partition - 1].lastbeeps = vistaCmd.statusFlags.beeps;

             if (vistaCmd.statusFlags.systemFlag && strstr(vistaCmd.statusFlags.prompt2, HITSTAR))
                alarm_keypress_partition("*",partition);
            }
          }
          #if defined(ARDUINO_MQTT)
          Serial.printf("Prompt: %s\n", vistaCmd.statusFlags.prompt1);
          Serial.printf("Prompt: %s\n", vistaCmd.statusFlags.prompt2);
          Serial.printf("Beeps: %d\n", vistaCmd.statusFlags.beeps);          
          #else    
          ESP_LOGI(TAG, PSTR("Prompt: %s"), vistaCmd.statusFlags.prompt1);
          ESP_LOGI(TAG, PSTR("Prompt: %s"), vistaCmd.statusFlags.prompt2);
          ESP_LOGI(TAG, PSTR("Beeps: %d"), vistaCmd.statusFlags.beeps);
          #endif
        }


        //publishes lrr status messages
        if ((vistaCmd.cbuf[0] == 0xf9 && vistaCmd.cbuf[3] == 0x58 && vistaCmd.newCmd) || firstRun) { //we show all lrr messages with type 58

            int c = vistaCmd.statusFlags.lrr.code;
            int q = vistaCmd.statusFlags.lrr.qual;
            int z = vistaCmd.statusFlags.lrr.zone;


          std::string qual;
          char msg[50];          
          if (c < 400)
            qual = (q == 3) ? PSTR(" Cleared") : "";
          else if (c == 570)
            qual = (q == 1) ? PSTR(" Active") : " Cleared";
          else
            qual = (q == 1) ? PSTR(" Restored") : "";
          if (c) {
            String lrrString = String(statusText(c));

            std::string uf = PSTR("user");
            if (lrrString[0] == 'Z')
              uf = PSTR("zone");
          
            sprintf(msg, PSTR("%d: %s %s %d%s"), c, & lrrString[1], uf.c_str(), z, qual.c_str());
            
            lrrMsgChangeCallback(msg);
            refreshLrrTime = millis();
          }

        }


        // done other cmd processing.  Process f7 now
        if (vistaCmd.cbuf[0] != 0xf7  ||  vistaCmd.cbuf[12]==0x77 ) return;

        currentSystemState = sunavailable;
        currentLightState.stay = false;
        currentLightState.away = false;
        currentLightState.night = false;
        currentLightState.instant = false;
        currentLightState.ready = false;
        currentLightState.alarm = false;
        currentLightState.armed = false;
        currentLightState.ac = true;
        currentLightState.fire = false;
        currentLightState.check = false;
        currentLightState.trouble = false;  
        currentLightState.bypass = false; 
        currentLightState.chime = false; 

                // Publishes ready status
                
        if (vistaCmd.statusFlags.ready) {
          currentSystemState = sdisarmed;
          currentLightState.ready = true;
          
        }
        //armed status lights
        if (vistaCmd.cbuf[0] == 0xf7 && vistaCmd.statusFlags.systemFlag && (vistaCmd.statusFlags.armedAway || vistaCmd.statusFlags.armedStay  )) {
          if (vistaCmd.statusFlags.night) {
            currentSystemState = sarmednight;
            currentLightState.night = true;
            currentLightState.stay = true;
          } else if (vistaCmd.statusFlags.armedAway) {
            currentSystemState = sarmedaway;
            currentLightState.away = true;
          } else {
            currentSystemState = sarmedstay;
            currentLightState.stay = true;
          }
          currentLightState.armed = true;
        } else



        //zone fire status
        //int tz;
        if (vistaCmd.cbuf[0] == 0xf7 && !(vistaCmd.statusFlags.systemFlag  || vistaCmd.statusFlags.armedAway || vistaCmd.statusFlags.armedStay ) && vistaCmd.statusFlags.fireZone) {
         if (vistaCmd.cbuf[5] > 0x90) getZoneFromPrompt(vistaCmd.statusFlags.prompt1);
        //if (promptContains(p1,FIRE,tz) && !vistaCmd.statusFlags.systemFlag) {
          fireStatus.zone = vistaCmd.statusFlags.zone;
          fireStatus.time = millis();
          fireStatus.state = true;
          getZone(vistaCmd.statusFlags.zone)->fire=true;  
         //ESP_LOGD("test","fire found for zone %d,status=%d",vistaCmd.statusFlags.zone,fireStatus.state);          

        } else
        //zone alarm status 
        if (vistaCmd.cbuf[0] == 0xf7 && !vistaCmd.statusFlags.systemFlag && vistaCmd.statusFlags.alarm) {
         if (vistaCmd.cbuf[5] > 0x90) getZoneFromPrompt(vistaCmd.statusFlags.prompt1);
        //if (promptContains(p1,ALARM,tz) && !vistaCmd.statusFlags.systemFlag) {
            zoneType * zt=getZone(vistaCmd.statusFlags.zone);             
            if (!zt->alarm && zt->active) {
             zt->alarm=true;
             zoneStatusUpdate(zt);
            }
            if (!zt->partition && zt->active)
                assignPartitionToZone(zt);            
            zt->time = millis();
            alarmStatus.zone = vistaCmd.statusFlags.zone;
            alarmStatus.time = zt->time;
            alarmStatus.state = true;
            //ESP_LOGD("test","alarm found for zone %d,status=%d",vistaCmd.statusFlags.zone,zt->alarm );
        } else
        //device check status 
         if (vistaCmd.cbuf[0] == 0xf7 && !(vistaCmd.statusFlags.systemFlag  || vistaCmd.statusFlags.armedAway || vistaCmd.statusFlags.armedStay ) && vistaCmd.statusFlags.check) {
         if (vistaCmd.cbuf[5] > 0x90) getZoneFromPrompt(vistaCmd.statusFlags.prompt1);
             zoneType * zt=getZone(vistaCmd.statusFlags.zone);
             if (!zt->check && zt->active) {
                zt->check=true;
                zt->open=false;
                zt->alarm=false;
                zoneStatusUpdate(zt);
              //ESP_LOGD("test","check found for zone %d,status=%d",vistaCmd.statusFlags.zone,zt->check );              
             }
            if (!zt->partition && zt->active)
                assignPartitionToZone(zt);             
             zt->time=millis();
      } else
         
        //zone fault status 
       // ESP_LOGD("test","armed status/system,stay,away flag is: %d , %d, %d , %d",vistaCmd.statusFlags.armed,vistaCmd.statusFlags.systemFlag,vistaCmd.statusFlags.armedStay,vistaCmd.statusFlags.armedAway);
         if (vistaCmd.cbuf[0] == 0xf7 && !(vistaCmd.statusFlags.systemFlag  || vistaCmd.statusFlags.armedAway || vistaCmd.statusFlags.armedStay || vistaCmd.statusFlags.fire || vistaCmd.statusFlags.check || vistaCmd.statusFlags.alarm  || vistaCmd.statusFlags.beeps==1) ) { 
         if (vistaCmd.cbuf[5] > 0x90) getZoneFromPrompt(vistaCmd.statusFlags.prompt1);
        // if (vistaCmd.statusFlags.zone==4) vistaCmd.statusFlags.zone=997;
       // if (promptContains(p1,FAULT,tz) && !vistaCmd.statusFlags.systemFlag) {

            zoneType * zt=getZone(vistaCmd.statusFlags.zone);            
            if (!zt->open && zt->active) {
                zt->open=true;  
                zt->check=false;
                zt->bypass=false;
                zoneStatusUpdate(zt);
               
            }
            if (!zt->partition && zt->active)
                assignPartitionToZone(zt);  
           
           // ESP_LOGD("test","fault found for zone %d,status=%d",vistaCmd.statusFlags.zone,zt->open);
            zt->time = millis();
        } else
        
        //zone bypass status
         if (vistaCmd.cbuf[0] == 0xf7 && !(vistaCmd.statusFlags.systemFlag  || vistaCmd.statusFlags.armedAway || vistaCmd.statusFlags.armedStay || vistaCmd.statusFlags.fire || vistaCmd.statusFlags.check || vistaCmd.statusFlags.alarm ) && vistaCmd.statusFlags.bypass && vistaCmd.statusFlags.beeps==1) {  
         if (vistaCmd.cbuf[5] > 0x90) getZoneFromPrompt(vistaCmd.statusFlags.prompt1);
       // if (promptContains(p1,BYPAS,tz) && !vistaCmd.statusFlags.systemFlag) {
        
           zoneType * zt=getZone(vistaCmd.statusFlags.zone);
           
          if (!zt->bypass && zt->active) {
            zt->bypass=true;
            zoneStatusUpdate(zt);
          }
            if (!zt->partition && zt->active)
                assignPartitionToZone(zt);          
            zt->time = millis();
   
          //ESP_LOGD("test","bypass found for zone %d,status=%d",vistaCmd.statusFlags.zone,zt->bypass); 
        }

        //trouble lights 
        if (!vistaCmd.statusFlags.acPower) {
          currentLightState.ac = false;
        } 

        if (vistaCmd.statusFlags.lowBattery  && vistaCmd.statusFlags.systemFlag) {
          currentLightState.bat = true;
          lowBatteryTime = millis();
        } 
        // ESP_LOGE(TAG,"ac=%d,batt status = %d,systemflag=%d,lightbat status=%d,trouble=%d", currentLightState.ac,vistaCmd.statusFlags.lowBattery,vistaCmd.statusFlags.systemFlag,currentLightState.bat,currentLightState.trouble);

        if (vistaCmd.statusFlags.fire) {
          currentLightState.fire = true;
          currentSystemState = striggered;
        } 

        if (vistaCmd.statusFlags.inAlarm) {
          currentSystemState = striggered;
          alarmStatus.zone = 99;
          alarmStatus.time = millis();
          alarmStatus.state = true;
        }

        if (vistaCmd.statusFlags.chime) {
          currentLightState.chime = true;
        } 

        if (vistaCmd.statusFlags.bypass) {
          currentLightState.bypass = true;
        } 

        if (vistaCmd.statusFlags.check) {
          currentLightState.check = true;
        } 
        if (vistaCmd.statusFlags.instant ) {
          currentLightState.instant = true;
        } 

        //if ( vistaCmd.statusFlags.cancel ) {
        //   currentLightState.canceled=true;
        //	}    else  currentLightState.canceled=false;        
        unsigned long chkTime = millis();
        //clear alarm statuses  when timer expires
        if ((chkTime - fireStatus.time) > TTL) {
          fireStatus.state = false;          
          if (fireStatus.zone > 0 && fireStatus.zone <=maxZones)          
            getZone(fireStatus.zone)->fire=false;
        }
        if ((chkTime- alarmStatus.time) > TTL) {
          alarmStatus.state = false;
          if (alarmStatus.zone > 0 && alarmStatus.zone <=maxZones)
            getZone(alarmStatus.zone)->alarm=false;          
        }
        if ((chkTime - panicStatus.time) > TTL) {
          panicStatus.state = false;
          if (panicStatus.zone > 0 && panicStatus.zone <=maxZones)
           getZone(panicStatus.zone)->panic=false;             
        }
        //  if ((millis() - systemPrompt.time) > TTL) systemPrompt.state = false;
        if ((chkTime - lowBatteryTime) > TTL) 
            currentLightState.bat = false;

        if  (!currentLightState.ac || currentLightState.bat )
          currentLightState.trouble = true;
        
        currentLightState.alarm = alarmStatus.state;

        for (uint8_t partition = 1; partition <= maxPartitions; partition++) {
          if ((partitions[partition - 1] && partitionTargets==1) && vistaCmd.statusFlags.systemFlag) {
            //system status message
            forceRefresh=partitionStates[partition - 1].refreshStatus ;
              
            if (currentSystemState != partitionStates[partition - 1].previousSystemState || forceRefresh)
              switch (currentSystemState) {
              case striggered:
                systemStatusChangeCallback(STATUS_TRIGGERED, partition);
                break;
              case sarmedaway:
                systemStatusChangeCallback(STATUS_ARMED, partition);
                break;
              case sarmednight:
                systemStatusChangeCallback(STATUS_NIGHT, partition);
                break;
              case sarmedstay:
                systemStatusChangeCallback(STATUS_STAY, partition);
                break;
              case sunavailable:
                systemStatusChangeCallback(STATUS_NOT_READY, partition);
                break;
              case sdisarmed:
                systemStatusChangeCallback(STATUS_OFF, partition);
                break;
              default:
                systemStatusChangeCallback(STATUS_NOT_READY, partition);
              }
             partitionStates[partition - 1].previousSystemState = currentSystemState;
             partitionStates[partition - 1].refreshStatus=false;
          }
        }
        

        for (uint8_t partition = 1; partition <= maxPartitions; partition++) {
          if ((partitions[partition - 1] && partitionTargets==1) ) {

            //publish status on change only - keeps api traffic down
            previousLightState = partitionStates[partition - 1].previousLightState;
            
            forceRefresh=partitionStates[partition - 1].refreshLights ;
           // ESP_LOGD("test","refreshing partition statuse partitions: %d,force refresh=%d",partition,forceRefresh);
            if (currentLightState.fire != previousLightState.fire || forceRefresh)
              statusChangeCallback(sfire, currentLightState.fire, partition);
            if (currentLightState.alarm != previousLightState.alarm || forceRefresh)
              statusChangeCallback(salarm, currentLightState.alarm, partition);
            if ((currentLightState.trouble != previousLightState.trouble || forceRefresh) )
              statusChangeCallback(strouble, currentLightState.trouble, partition);
            if (currentLightState.chime != previousLightState.chime || forceRefresh) 
              statusChangeCallback(schime, currentLightState.chime, partition);
            //if (currentLightState.check != previousLightState.check || forceRefresh) 
            //  statusChangeCallback(scheck, currentLightState.check, partition);          
            if (((currentLightState.away != previousLightState.away)  && vistaCmd.statusFlags.systemFlag) || forceRefresh)
              statusChangeCallback(sarmedaway, currentLightState.away, partition);
            if (currentLightState.ac != previousLightState.ac || forceRefresh)
              statusChangeCallback(sac, currentLightState.ac, partition);
            if (((currentLightState.stay != previousLightState.stay ) && vistaCmd.statusFlags.systemFlag) || forceRefresh)
              statusChangeCallback(sarmedstay, currentLightState.stay, partition);
            if (((currentLightState.night != previousLightState.night ) && vistaCmd.statusFlags.systemFlag) || forceRefresh)
              statusChangeCallback(sarmednight, currentLightState.night, partition);
            if (((currentLightState.instant != previousLightState.instant ) && vistaCmd.statusFlags.systemFlag) || forceRefresh)
              statusChangeCallback(sinstant, currentLightState.instant, partition);
            if (currentLightState.bat != previousLightState.bat || forceRefresh  )
              statusChangeCallback(sbat, currentLightState.bat, partition);
            if (currentLightState.bypass != previousLightState.bypass || forceRefresh)
              statusChangeCallback(sbypass, currentLightState.bypass, partition);
            if (currentLightState.ready != previousLightState.ready || forceRefresh)
              statusChangeCallback(sready, currentLightState.ready, partition);
            if (((currentLightState.armed != previousLightState.armed ) && vistaCmd.statusFlags.systemFlag) || forceRefresh)
              statusChangeCallback(sarmed, currentLightState.armed, partition);
            //  if (currentLightState.canceled != previousLightState.canceled) 
            //   statusChangeCallback(scanceled,currentLightState.canceled,partition);

            partitionStates[partition - 1].previousLightState = currentLightState;
            partitionStates[partition - 1].refreshLights=false;
           
          }
        }

        std::string zoneStatusMsg = "";
        char s1[16];
        //clears restored zones after timeout
        for (auto  &x: extZones) {


#if !defined(ESP32) or  !defined(USETASK)      
          vista.handle();
#endif
  
           if (!x.active || !x.partition) continue;
        
           if (partitionStates[ x.partition-1].previousLightState.ready) {
            if (x.open) {
              x.open=false;
              x.check=false;  
              x.alarm=false;              
              zoneStatusUpdate(&x);  
            }              
               
           }
           
           if ( x.bypass && !partitionStates[ x.partition-1].previousLightState.bypass) {
             x.bypass=false;  
           } 
           
           if ( x.alarm && !partitionStates[ x.partition-1].previousLightState.alarm) {
             x.alarm=false;  
           }             
            
          if (!x.bypass &&  x.open  && (millis() -  x.time) > TTL ) {
             x.open=false;
             zoneStatusUpdate(&x);
          }
          if (!x.bypass && x.check && (millis() -  x.time) > TTL ) {
             x.check=false; 
             zoneStatusUpdate(&x);
          }
          
           if ( forceRefreshZones) {
             zoneStatusUpdate(&x);
           }
            
          if ( x.open) {
            if (zoneStatusMsg !="") 
                sprintf(s1, PSTR(",OP:%d"), x.zone);                 
            else
                sprintf(s1, PSTR("OP:%d"), x.zone);              
            zoneStatusMsg.append(s1);
          } 
          if ( x.alarm) {
            if (zoneStatusMsg !="") 
                sprintf(s1, PSTR(",AL:%d"), x.zone);                 
            else
                sprintf(s1, PSTR("AL:%d"), x.zone); 
            zoneStatusMsg.append(s1);
          } 
          if ( x.bypass) {
            if (zoneStatusMsg !="") 
                sprintf(s1, PSTR(",BY:%d"), x.zone);                 
            else
                sprintf(s1, PSTR("BY:%d"), x.zone); 
            zoneStatusMsg.append(s1);
          }
          if (x.check ) {
            if (zoneStatusMsg !="") 
                sprintf(s1, PSTR(",CK:%d"), x.zone);                 
            else
                sprintf(s1, PSTR("CK:%d"), x.zone); 
            zoneStatusMsg.append(s1);
          } 
          if (x.lowbat ) { //low rf battery
            if (zoneStatusMsg !="") 
                sprintf(s1, PSTR(",LB:%d"), x.zone);                 
            else
                sprintf(s1, PSTR("LB:%d"), x.zone); 
            zoneStatusMsg.append(s1);
          }
          yield();
      
        }
                      
        if ((zoneStatusMsg != previousZoneStatusMsg  || forceRefreshZones) && zoneExtendedStatusCallback != NULL)
          zoneExtendedStatusCallback(zoneStatusMsg);
      
        previousZoneStatusMsg = zoneStatusMsg;
        
        chkTime=millis();
        if (chkTime - refreshLrrTime > 30000) {
          lrrMsgChangeCallback("");
          refreshLrrTime = chkTime;
        }  

        if (chkTime - refreshRfTime > 30000) {
          rfMsgChangeCallback("");
          refreshRfTime = chkTime;
        }      
        
        firstRun = false;
        forceRefreshZones=false;
        forceRefreshGlobal=false;
      }


#if !defined(ESP32) or  !defined(USETASK)  
       vista.handle();
#endif
    }

    const __FlashStringHelper * vistaECPHome::statusText(int statusCode) {
      switch (statusCode) {

      case 100:
        return F("ZMedical");
      case 101:
        return F("ZPersonal Emergency");
      case 102:
        return F("ZFail to report in");
      case 110:
        return F("ZFire");
      case 111:
        return F("ZSmoke");
      case 112:
        return F("ZCombustion");
      case 113:
        return F("ZWater Flow");
      case 114:
        return F("ZHeat");
      case 115:
        return F("ZPull Station");
      case 116:
        return F("ZDuct");
      case 117:
        return F("ZFlame");
      case 118:
        return F("ZNear Alarm");
      case 120:
        return F("ZPanic");
      case 121:
        return F("UDuress");
      case 122:
        return F("ZSilent");
      case 123:
        return F("ZAudible");
      case 124:
        return F("ZDuress – Access granted");
      case 125:
        return F("ZDuress – Egress granted");
      case 126:
        return F("UHoldup suspicion print");
      case 127:
        return F("URemote Silent Panic");
      case 129:
        return F("ZPanic Verifier");
      case 130:
        return F("ZBurglary");
      case 131:
        return F("ZPerimeter");
      case 132:
        return F("ZInterior");
      case 133:
        return F("Z24 Hour (Safe)");
      case 134:
        return F("ZEntry/Exit");
      case 135:
        return F("ZDay/Night");
      case 136:
        return F("ZOutdoor");
      case 137:
        return F("ZTamper");
      case 138:
        return F("ZNear alarm");
      case 139:
        return F("ZIntrusion Verifier");
      case 140:
        return F("ZGeneral Alarm");
      case 141:
        return F("ZPolling loop open");
      case 142:
        return F("ZPolling loop short");
      case 143:
        return F("ZExpansion module failure");
      case 144:
        return F("ZSensor tamper");
      case 145:
        return F("ZExpansion module tamper");
      case 146:
        return F("ZSilent Burglary");
      case 147:
        return F("ZSensor Supervision Failure");
      case 150:
        return F("Z24 Hour NonBurglary");
      case 151:
        return F("ZGas detected");
      case 152:
        return F("ZRefrigeration");
      case 153:
        return F("ZLoss of heat");
      case 154:
        return F("ZWater Leakage");

      case 155:
        return F("ZFoil Break");
      case 156:
        return F("ZDay Trouble");
      case 157:
        return F("ZLow bottled gas level");
      case 158:
        return F("ZHigh temp");
      case 159:
        return F("ZLow temp");
      case 160:
        return F("ZAwareness Zone Response");
      case 161:
        return F("ZLoss of air flow");
      case 162:
        return F("ZCarbon Monoxide detected");
      case 163:
        return F("ZTank level");
      case 168:
        return F("ZHigh Humidity");
      case 169:
        return F("ZLow Humidity");
      case 200:
        return F("ZFire Supervisory");
      case 201:
        return F("ZLow water pressure");
      case 202:
        return F("ZLow CO2");
      case 203:
        return F("ZGate valve sensor");
      case 204:
        return F("ZLow water level");
      case 205:
        return F("ZPump activated");
      case 206:
        return F("ZPump failure");
      case 300:
        return F("ZSystem Trouble");
      case 301:
        return F("ZAC Loss");
      case 302:
        return F("ZLow system battery");
      case 303:
        return F("ZRAM Checksum bad");
      case 304:
        return F("ZROM checksum bad");
      case 305:
        return F("ZSystem reset");
      case 306:
        return F("ZPanel programming changed");
      case 307:
        return F("ZSelftest failure");
      case 308:
        return F("ZSystem shutdown");
      case 309:
        return F("ZBattery test failure");
      case 310:
        return F("ZGround fault");
      case 311:
        return F("ZBattery Missing/Dead");
      case 312:
        return F("ZPower Supply Overcurrent");
      case 313:
        return F("UEngineer Reset");
      case 314:
        return F("ZPrimary Power Supply Failure");

      case 315:
        return F("ZSystem Trouble");
      case 316:
        return F("ZSystem Tamper");

      case 317:
        return F("ZControl Panel System Tamper");
      case 320:
        return F("ZSounder/Relay");
      case 321:
        return F("ZBell 1");
      case 322:
        return F("ZBell 2");
      case 323:
        return F("ZAlarm relay");
      case 324:
        return F("ZTrouble relay");
      case 325:
        return F("ZReversing relay");
      case 326:
        return F("ZNotification Appliance Ckt. # 3");
      case 327:
        return F("ZNotification Appliance Ckt. #4");
      case 330:
        return F("ZSystem Peripheral trouble");
      case 331:
        return F("ZPolling loop open");
      case 332:
        return F("ZPolling loop short");
      case 333:
        return F("ZExpansion module failure");
      case 334:
        return F("ZRepeater failure");
      case 335:
        return F("ZLocal printer out of paper");
      case 336:
        return F("ZLocal printer failure");
      case 337:
        return F("ZExp. Module DC Loss");
      case 338:
        return F("ZExp. Module Low Batt.");
      case 339:
        return F("ZExp. Module Reset");
      case 341:
        return F("ZExp. Module Tamper");
      case 342:
        return F("ZExp. Module AC Loss");
      case 343:
        return F("ZExp. Module selftest fail");
      case 344:
        return F("ZRF Receiver Jam Detect");

      case 345:
        return F("ZAES Encryption disabled/ enabled");
      case 350:
        return F("ZCommunication  trouble");
      case 351:
        return F("ZTelco 1 fault");
      case 352:
        return F("ZTelco 2 fault");
      case 353:
        return F("ZLong Range Radio xmitter fault");
      case 354:
        return F("ZFailure to communicate event");
      case 355:
        return F("ZLoss of Radio supervision");
      case 356:
        return F("ZLoss of central polling");
      case 357:
        return F("ZLong Range Radio VSWR problem");
      case 358:
        return F("ZPeriodic Comm Test Fail /Restore");

      case 359:
        return F("Z");

      case 360:
        return F("ZNew Registration");
      case 361:
        return F("ZAuthorized  Substitution Registration");
      case 362:
        return F("ZUnauthorized  Substitution Registration");
      case 365:
        return F("ZModule Firmware Update Start/Finish");
      case 366:
        return F("ZModule Firmware Update Failed");

      case 370:
        return F("ZProtection loop");
      case 371:
        return F("ZProtection loop open");
      case 372:
        return F("ZProtection loop short");
      case 373:
        return F("ZFire trouble");
      case 374:
        return F("ZExit error alarm (zone)");
      case 375:
        return F("ZPanic zone trouble");
      case 376:
        return F("ZHoldup zone trouble");
      case 377:
        return F("ZSwinger Trouble");
      case 378:
        return F("ZCrosszone Trouble");

      case 380:
        return F("ZSensor trouble");
      case 381:
        return F("ZLoss of supervision  RF");
      case 382:
        return F("ZLoss of supervision  RPM");
      case 383:
        return F("ZSensor tamper");
      case 384:
        return F("ZRF low battery");
      case 385:
        return F("ZSmoke detector Hi sensitivity");
      case 386:
        return F("ZSmoke detector Low sensitivity");
      case 387:
        return F("ZIntrusion detector Hi sensitivity");
      case 388:
        return F("ZIntrusion detector Low sensitivity");
      case 389:
        return F("ZSensor selftest failure");
      case 391:
        return F("ZSensor Watch trouble");
      case 392:
        return F("ZDrift Compensation Error");
      case 393:
        return F("ZMaintenance Alert");
      case 394:
        return F("ZCO Detector needs replacement");
      case 400:
        return F("UOpen/Close");
      case 401:
        return F("UArmed AWAY");
      case 402:
        return F("UGroup O/C");
      case 403:
        return F("UAutomatic O/C");
      case 404:
        return F("ULate to O/C (Note: use 453 or 454 instead )");
      case 405:
        return F("UDeferred O/C (Obsolete do not use )");
      case 406:
        return F("UCancel");
      case 407:
        return F("URemote arm/disarm");
      case 408:
        return F("UQuick arm");
      case 409:
        return F("UKeyswitch O/C");
      case 411:
        return F("UCallback request made");
      case 412:
        return F("USuccessful  download/access");
      case 413:
        return F("UUnsuccessful access");
      case 414:
        return F("USystem shutdown command received");
      case 415:
        return F("UDialer shutdown command received");

      case 416:
        return F("ZSuccessful Upload");
      case 418:
        return F("URemote Cancel");
      case 419:
        return F("URemote Verify");
      case 421:
        return F("UAccess denied");
      case 422:
        return F("UAccess report by user");
      case 423:
        return F("ZForced Access");
      case 424:
        return F("UEgress Denied");
      case 425:
        return F("UEgress Granted");
      case 426:
        return F("ZAccess Door propped open");
      case 427:
        return F("ZAccess point Door Status Monitor trouble");
      case 428:
        return F("ZAccess point Request To Exit trouble");
      case 429:
        return F("UAccess program mode entry");
      case 430:
        return F("UAccess program mode exit");
      case 431:
        return F("UAccess threat level change");
      case 432:
        return F("ZAccess relay/trigger fail");
      case 433:
        return F("ZAccess RTE shunt");
      case 434:
        return F("ZAccess DSM shunt");
      case 435:
        return F("USecond Person Access");
      case 436:
        return F("UIrregular Access");
      case 441:
        return F("UArmed STAY");
      case 442:
        return F("UKeyswitch Armed STAY");
      case 443:
        return F("UArmed with System Trouble Override");
      case 450:
        return F("UException O/C");
      case 451:
        return F("UEarly O/C");
      case 452:
        return F("ULate O/C");
      case 453:
        return F("UFailed to Open");
      case 454:
        return F("UFailed to Close");
      case 455:
        return F("UAutoarm Failed");
      case 456:
        return F("UPartial Arm");
      case 457:
        return F("UExit Error (user)");
      case 458:
        return F("UUser on Premises");
      case 459:
        return F("URecent Close");
      case 461:
        return F("ZWrong Code Entry");
      case 462:
        return F("ULegal Code Entry");
      case 463:
        return F("URearm after Alarm");
      case 464:
        return F("UAutoarm Time Extended");
      case 465:
        return F("ZPanic Alarm Reset");
      case 466:
        return F("UService On/Off Premises");

      case 501:
        return F("ZAccess reader disable");
      case 520:
        return F("ZSounder/Relay  Disable");
      case 521:
        return F("ZBell 1 disable");
      case 522:
        return F("ZBell 2 disable");
      case 523:
        return F("ZAlarm relay disable");
      case 524:
        return F("ZTrouble relay disable");
      case 525:
        return F("ZReversing relay disable");
      case 526:
        return F("ZNotification Appliance Ckt. # 3 disable");
      case 527:
        return F("ZNotification Appliance Ckt. # 4 disable");
      case 531:
        return F("ZModule Added");
      case 532:
        return F("ZModule Removed");
      case 551:
        return F("ZDialer disabled");
      case 552:
        return F("ZRadio transmitter disabled");
      case 553:
        return F("ZRemote  Upload/Download disabled");
      case 570:
        return F("ZZone/Sensor bypass");
      case 571:
        return F("ZFire bypass");
      case 572:
        return F("Z24 Hour zone bypass");
      case 573:
        return F("ZBurg. Bypass");
      case 574:
        return F("UGroup bypass");
      case 575:
        return F("ZSwinger bypass");
      case 576:
        return F("ZAccess zone shunt");
      case 577:
        return F("ZAccess point bypass");
      case 578:
        return F("ZVault Bypass");
      case 579:
        return F("ZVent Zone Bypass");
      case 601:
        return F("ZManual trigger test report");
      case 602:
        return F("ZPeriodic test report");
      case 603:
        return F("ZPeriodic RF transmission");
      case 604:
        return F("UFire test");
      case 605:
        return F("ZStatus report to follow");
      case 606:
        return F("ZListenin to follow");
      case 607:
        return F("UWalk test mode");
      case 608:
        return F("ZPeriodic test  System Trouble Present");
      case 609:
        return F("ZVideo Xmitter active");
      case 611:
        return F("ZPoint tested OK");
      case 612:
        return F("ZPoint not tested");
      case 613:
        return F("ZIntrusion Zone Walk Tested");
      case 614:
        return F("ZFire Zone Walk Tested");
      case 615:
        return F("ZPanic Zone Walk Tested");
      case 616:
        return F("ZService Request");
      case 621:
        return F("ZEvent Log reset");
      case 622:
        return F("ZEvent Log 50% full");
      case 623:
        return F("ZEvent Log 90% full");
      case 624:
        return F("ZEvent Log overflow");
      case 625:
        return F("UTime/Date reset");
      case 626:
        return F("ZTime/Date inaccurate");
      case 627:
        return F("ZProgram mode entry");

      case 628:
        return F("ZProgram mode exit");
      case 629:
        return F("Z32 Hour Event log marker");
      case 630:
        return F("ZSchedule change");
      case 631:
        return F("ZException schedule change");
      case 632:
        return F("ZAccess schedule change");
      case 641:
        return F("ZSenior Watch Trouble");
      case 642:
        return F("ULatchkey Supervision");
      case 643:
        return F("ZRestricted Door Opened");
      case 645:
        return F("ZHelp Arrived");
      case 646:
        return F("ZAddition Help Needed");
      case 647:
        return F("ZAddition Help Cancel");
      case 651:
        return F("ZReserved for Ademco Use");
      case 652:
        return F("UReserved for Ademco Use");
      case 653:
        return F("UReserved for Ademco Use");
      case 654:
        return F("ZSystem Inactivity");
      case 655:
        return F("UUser Code X modified by Installer");
      case 703:
        return F("ZAuxiliary #3");
      case 704:
        return F("ZInstaller Test");
      case 750:
        return F("ZUser Assigned");
      case 751:
        return F("ZUser Assigned");
      case 752:
        return F("ZUser Assigned");
      case 753:
        return F("ZUser Assigned");
      case 754:
        return F("ZUser Assigned");
      case 755:
        return F("ZUser Assigned");
      case 756:
        return F("ZUser Assigned");
      case 757:
        return F("ZUser Assigned");
      case 758:
        return F("ZUser Assigned");
      case 759:
        return F("ZUser Assigned");
      case 760:
        return F("ZUser Assigned");
      case 761:
        return F("ZUser Assigned");
      case 762:
        return F("ZUser Assigned");
      case 763:
        return F("ZUser Assigned");
      case 764:
        return F("ZUser Assigned");
      case 765:
        return F("ZUser Assigned");
      case 766:
        return F("ZUser Assigned");
      case 767:
        return F("ZUser Assigned");
      case 768:
        return F("ZUser Assigned");
      case 769:
        return F("ZUser Assigned");
      case 770:
        return F("ZUser Assigned");
      case 771:
        return F("ZUser Assigned");
      case 772:
        return F("ZUser Assigned");
      case 773:
        return F("ZUser Assigned");
      case 774:
        return F("ZUser Assigned");
      case 775:
        return F("ZUser Assigned");
      case 776:
        return F("ZUser Assigned");
      case 777:
        return F("ZUser Assigned");
      case 778:
        return F("ZUser Assigned");
      case 779:
        return F("ZUser Assigned");
      case 780:
        return F("ZUser Assigned");
      case 781:
        return F("ZUser Assigned");
      case 782:
        return F("ZUser Assigned");
      case 783:
        return F("ZUser Assigned");
      case 784:
        return F("ZUser Assigned");
      case 785:
        return F("ZUser Assigned");
      case 786:
        return F("ZUser Assigned");
      case 787:
        return F("ZUser Assigned");
      case 788:
        return F("ZUser Assigned");
      case 789:
        return F("ZUser Assigned");

      case 796:
        return F("ZUnable to output signal (Derived Channel)");
      case 798:
        return F("ZSTU Controller down (Derived Channel)");
      case 900:
        return F("ZDownload Abort");
      case 901:
        return F("ZDownload Start/End");
      case 902:
        return F("ZDownload Interrupted");
      case 903:
        return F("ZDevice Flash Update Started/ Completed");
      case 904:
        return F("ZDevice Flash Update Failed");
      case 910:
        return F("ZAutoclose with Bypass");
      case 911:
        return F("ZBypass Closing");
      case 912:
        return F("ZFire Alarm Silence");
      case 913:
        return F("USupervisory Point test Start/End");
      case 914:
        return F("UHoldup test Start/End");
      case 915:
        return F("UBurg. Test Print Start/End");
      case 916:
        return F("USupervisory Test Print Start/End");
      case 917:
        return F("ZBurg. Diagnostics Start/End");
      case 918:
        return F("ZFire Diagnostics Start/End");
      case 919:
        return F("ZUntyped diagnostics");
      case 920:
        return F("UTrouble Closing (closed with burg. during exit)");
      case 921:
        return F("UAccess Denied Code Unknown");
      case 922:
        return F("ZSupervisory Point Alarm");
      case 923:
        return F("ZSupervisory Point Bypass");
      case 924:
        return F("ZSupervisory Point Trouble");
      case 925:
        return F("ZHoldup Point Bypass");
      case 926:
        return F("ZAC Failure for 4 hours");
      case 927:
        return F("ZOutput Trouble");
      case 928:
        return F("UUser code for event");
      case 929:
        return F("ULogoff");
      case 954:
        return F("ZCS Connection Failure");
      case 961:
        return F("ZRcvr Database Connection Fail/Restore");
      case 962:
        return F("ZLicense Expiration Notify");
      case 999:
        return F("Z1 and 1/3 Day No Read Log");
      default:
        return F("ZUnknown");
      }
    }
  
#if defined(AUTOPOPULATE)
/*
void vistaECPHome::loadZone(int z,bool fetchPromptName) {
    std::string n=std::to_string(z);      
    std::string type_id="z" + n;

    auto it = std::find_if(bMap.begin(), bMap.end(),  [&type_id](binary_sensor::BinarySensor* f){ return f->get_type_id() == type_id; } );
     if (it != bMap.end()) return;  
    //binarySensorType * bst= new binarySensorType();
    template_::TemplateBinarySensor * ptr = new template_::TemplateBinarySensor();
    App.register_binary_sensor(ptr);

    if (fetchPromptName)
        ptr->name_static=getNameFromPrompt(vistaCmd.statusFlags.prompt1,vistaCmd.statusFlags.prompt2);
    
    if (ptr->name_static=="") 
     ptr->name_static="Zone " + n ;
    else
       ptr->name_static=ptr->name_static+" (" + type_id + ")";

    ptr->object_id_static=str_snake_case(ptr->name_static);
    ptr->type_id_static=type_id;
    ptr->set_name(ptr->name_static.c_str());
    ptr->set_object_id(ptr->object_id_static.c_str());
    ptr->set_type_id(ptr->type_id_static.c_str());
 ESP_LOGD(TAG,"get name=%s,get object_id=%s, get typeid=%s,",ptr->get_name().c_str(),ptr->get_object_id().c_str(),ptr->get_type_id().c_str());   
    ptr->set_publish_initial_state(true);  
    ptr->set_disabled_by_default(false); 
#if defined(ESPHOME_MQTT)   
    mqtt::MQTTBinarySensorComponent * mqptr=new mqtt::MQTTBinarySensorComponent(ptr);
    mqptr->set_component_source("mqtt");
    App.register_component(mqptr);
    mqptr->call();
#endif  
    App.register_component(ptr); 
    ptr->set_component_source("template.binary_sensor");    
    ptr->call();
    bMap=App.get_binary_sensors();    
}  
*/
#endif

#if !defined(ARDUINO_MQTT)
}} //namespaces
#endif

