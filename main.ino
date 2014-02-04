void loop() {
  Serial.begin(115200);
  int startTime, waitTime, hubsanWait,finishTime;
  Serial.println("Initializing...");
  A7105_Setup();
  initialize();
  Serial.println("Testing TX and RX mode...");
  Serial.println("Setting TX mode...");
  A7105_Strobe(A7105_TX);
  Serial.print("00h = ");
  Serial.println(A7105_ReadReg(0x00));
  delayMicroseconds(100);
  Serial.println("Setting RX mode...");
  A7105_Strobe(A7105_RX);
  Serial.print("00h = ");
  Serial.println(A7105_ReadReg(0x00));
  Serial.println("Testing complete.");
  Serial.println("Re-initializing...");
  A7105_Setup();
  initialize();
  Serial.println("Connection initialised.");
  
  startTime = micros();
  while (1) {
    if (Serial.available()>4) {
      if (Serial.read()!=23) {
        throttle = rudder =aileron = elevator = 0;
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
    finishTime=micros();
    waitTime = hubsanWait - (micros() - startTime);
    Serial.print("hubsanWait: " ); Serial.println(hubsanWait);
    Serial.print("waitTime: " ); Serial.println(waitTime);
    //Serial.println(hubsanWait);
    delayMicroseconds(hubsanWait);
    startTime = micros();
  }
}

