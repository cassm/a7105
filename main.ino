#define CONTROL_PACKET 3
#define SETTING_PACKET 4
#define LEDS_ON = 0x05
#define LEDS_OFF = 0x06
#define FLIPS_ON = 0x07
#define FLIPS_OFF = 0x08

void setup() {
  uint8_t startval, command;
  verbose = false;
  pinMode(RED_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  RED_OFF();
  BLUE_OFF();
  Serial.begin(115200);
  Serial.flush();
  Serial.println("Initialising...");

  // SPI initialisation and mode configuration
  A7105_Setup();
  
  // calibrate the chip and set the RF frequency, timing, transmission modes, session ID and channel
  initialize();
  
  rudder = aileron = elevator = 0x7F; 
  throttle = 0x00;
  
  Serial.println("Initialisation Complete");
}

void loop() {  
    // start the timer for the first packet transmission
    startTime = micros();

    // don't attempt to read from serial unless there is data being sent
    if (Serial.available() > 4) {

      // work through data on serial until we find the start of a packet
      do {
          startval = Serial.read();
      }
      while (startval != CONTROL_PACKET && startval != SETTING_PACKET);

      if (startval == CONTROL_PACKET) {
          throttle=Serial.read();
          rudder=Serial.read();
          aileron=Serial.read();
          elevator=Serial.read();
      }

      else if (startval == SETTING_PACKET) {
          command = Serial.read();
          if (command == LEDS_ON) {
              drone_settings |= 0x04;
          }
          else if (command == LEDS_OFF) {
              drone_settings &= ~0x04;
          }
          else if (command == FLIPS_ON) {
              drone_settings |= 0x08;
          }
          else if (command == FLIPS_OFF) {
              drone_settings &= ~0x08;
          }
          
          // clear unused bytes
          for ( int i = 0 ; i < 3 ; i++)
              Serial.read();
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

