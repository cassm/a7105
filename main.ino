void setup() {
  
  Serial.begin(115200);
  A7105_Setup();
  int startTime, waitTime, hubsanWait,finishTime;
  Serial.println("Initialising...");
  A7105_Reset();
  Serial.println("Reset complete.");
  A7105_Setup();
  
  // call initialise with an integer, which is the index of the channel to use out of the following:
  // {0x14, 0x1e, 0x28, 0x32, 0x3c, 0x46, 0x50, 0x5a, 0x64, 0x6e, 0x78, 0x82}
  initialize();
  Serial.println("Setup Complete");
}



// shouts pockets on the current channel
void shoutchannel() {
       int i;
       makepacket();
       while(1) {
           // write packet into fifo
           A7105_Strobe(A7105_STANDBY);
           A7105_WriteData(packet, 16, channel);
           delayMicroseconds(3000);
           // allow 20 loops for the transmitting flag to clear
           for(i = 0; i< 20; i++) {
               if(! (A7105_ReadReg(A7105_00_MODE) & 0x01))
                 break;
           }
           
        // if not cleared, give message and quit
        if (i == 20) {
            Serial.println("Failed to complete write\n");
            break;
        }
        /*else {
            Serial.println("Write successful\n");
            break;         
        }  */
    }
}
// sniffs the currently set channel, prints packets to serial
void sniffchannel() {
       A7105_Strobe(A7105_RX);  
       delayMicroseconds(3000);
       if(!A7105_ReadReg(A7105_00_MODE) & 0x01) {
           A7105_ReadData(packet, 16);
           printpacket(packet);
       }
}

// function to sniff a list of channels and see what is being broadcast on them
// sniffs 20 packets on each channel before looping, print result to serial
void scanchannels(const u8 channels[]) {
      for (int i = 0 ; i < sizeof(channels) ; i++) {
          Serial.println("");
          Serial.print("Switching to channel ");
          Serial.println(channels[i]);
          Serial.println("");
          A7105_WriteReg(A7105_0F_CHANNEL, channels[i]);
          for (int j = 0 ; j < 20 ; j++) {
              sniffchannel();
          }
      }
}

// function to scan a provided channel and print the packets to serial. repeats for 'loops' iterations
void sniffchannel(u8 _channel) {
      Serial.print("Switching to channel ");
      Serial.println(_channel);
      Serial.println("");
      A7105_WriteReg(A7105_0F_CHANNEL, _channel);
      while (1) {
          sniffchannel();
      }
}

void loop() {
    //shoutchannel();
       while(1) 
           // constantly scan all the hubsan channels, print the results to serial
           scanchannels(hubsan_ch);
}
