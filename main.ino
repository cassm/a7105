void setup() {
  
  Serial.begin(115200);
  Serial.println("Initialising...");

  // SPI initialisation and mode configuration
  A7105_Setup();
  
  // calibrate the chip and set the RF frequency, timing, transmission modes, session ID and channel
  initialize();
  
  Serial.println("Initialisation Complete");
}

void loop() {  
    // start the timer for the first packet transmission
    startTime = micros();
    if (Serial.available()>4) {
        // if we are streaming data to serial, use that for RAEV  
        if (Serial.read()!=23) {
            rudder = aileron = elevator = 0x7F; 
            throttle = 0x7F;
            
        // otherwise, use sensible defaults
        } else {
          throttle=Serial.read();
          rudder=Serial.read();
          aileron=Serial.read();
          elevator=Serial.read();
        }
    }
    
    // print information about which state the RF dialogue os currently in
    //Serial.print("State: ");
    //Serial.println(state);
    
    // perform the correct RF transaction
    hubsanWait = hubsan_cb();
    
    // stop timer for this packet
    finishTime = micros();
    
    // calculate how long to wait before transmitting the next packet
    waitTime = hubsanWait - (micros() - startTime);
    
    // wait that long
    delayMicroseconds(waitTime);
    
    // start the timer again
    startTime = micros();
  
  
  //Serial.println(A7105_ReadReg(0x00)); 
  //A7105_shoutchannel();
  //A7105_sniffchannel();
  
  //A7105_scanchannels(allowed_ch);
  //eavesdrop();
}

