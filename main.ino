void setup() {
  
  Serial.begin(115200);
  Serial.println("Initialising...");
  A7105_Setup();
  
  initialize();
  channel = 0x32;
  A7105_WriteReg(A7105_0F_CHANNEL, 0x32);
  Serial.println("Initialisation Complete");
}

void loop() {
  
  int startTime, waitTime, hubsanWait, finishTime;
  startTime = micros();
  while (1) {
    if (Serial.available()>4) {
      if (Serial.read()!=23) {
          throttle = rudder = aileron = elevator = 0;
      } else {
      throttle=Serial.read();
      rudder=Serial.read();
      aileron=Serial.read();
      elevator=Serial.read();
      }
    }
    
      //if (state!=0 && state!=1 & state!=128) 
    Serial.print("State: ");
    Serial.println(state);
    hubsanWait = hubsan_cb();
    finishTime = micros();
    waitTime = hubsanWait - (micros() - startTime);
    //Serial.print("hubsanWait: " ); Serial.println(hubsanWait);
    //Serial.print("waitTime: " ); Serial.println(waitTime);
    //Serial.println(hubsanWait);
    delayMicroseconds(waitTime);
    startTime = micros();
  }
  
  
  
  //Serial.println(A7105_ReadReg(0x00)); 
  //A7105_shoutchannel();
  //A7105_sniffchannel();
  
  //A7105_scanchannels(allowed_ch);
  //eavesdrop();
}

