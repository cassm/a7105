/*
    This project is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Deviation is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Deviation.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "a7105.h"


const u8 allowed_ch[] = {0x14, 0x1e, 0x28, 0x32, 0x3c, 0x46, 0x50, 0x5a, 0x64, 0x6e, 0x78, 0x82};
int no_allowed_channels = 12;


// Set CS pin mode, initialse and set sensible defaults for SPI, set GIO1 as output on chip
void A7105_Setup() {
  // initialise the cs lock pin
  pinMode(CS_PIN, OUTPUT);
  
  // initialise SPI, set mode and byte order
  SPI.begin();
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  
  // set GIO1 to SDO (MISO) by writing to reg GIO1S
  // (this instructs the chip to use GIO1 as the output pin)
  A7105_WriteReg(0x0b,0x19); // 0b011001
  
  if (verbose) Serial.println("Configuration complete.");
}

// reset the chip
void A7105_Reset()
{
    // this writes a null value to register 0x00, which triggers the reset
    A7105_WriteReg(0x00, 0x00);
    delayMicroseconds(100);
    A7105_WriteReg(0x0b,0x19);
    if (verbose) Serial.println("Reset complete");
}    

int A7105_calibrate_IF() {
    u8 calibration_result;
    
    //IF Filter Bank Calibration
    // write 001 to this register, chip will then calibrate IF filter bank, and lower the flag when the calibration is complete
    A7105_WriteReg(0x02, 1);
 
    // I HAVE NO IDEA WHETHER THIS DOES ANYTHING
    A7105_ReadReg(0x02);
    
    // if 02h has not cleared within 500ms, give a timeout error and abort.
    unsigned long ms = millis();
    while(millis()  - ms < 500) {
        if(! A7105_ReadReg(0x02))
            break;
    }
    if (millis() - ms >= 500) {
        if (verbose) Serial.println("Error: IF filter calibration has timed out.");
        return 1;
    }
    
    // read IF calibration status
    calibration_result = A7105_ReadReg(A7105_22_IF_CALIB_I);

    // this seems redundant. Is it?
    A7105_ReadReg(A7105_24_VCO_CURCAL);

    // check to see if auto calibration failure flag is set. If so, give error message and abort
    if(calibration_result & A7105_MASK_FBCF) {
        if (verbose) Serial.println("Error: IF filter calibration failed.");
        return 2;
    }
    return 0;
}

int A7105_calibrate_VCB(u8 channel) {
    u8 calibration_result;
    
    A7105_WriteReg(A7105_0F_CHANNEL, channel);
    
    //Initiate VCO bank calibration. register will auto clear when complete
    A7105_WriteReg(0x02, 2);
    
    // allow 500ms for calibration to complete
    unsigned long ms = millis();
    while(millis()  - ms < 500) {
        if(! A7105_ReadReg(0x02))
            break;
    }
    
    // if not complete, issue timeout error and abort
    if (millis() - ms >= 500){
          if (verbose) Serial.print("Error: VCO bank calibration timed out on channel ");
          Serial.println(channel);
          return 1;
        }
    
    // if auto calibration fail flag is high, print error and abort
    calibration_result = A7105_ReadReg(A7105_25_VCO_SBCAL_I);
    if (calibration_result & A7105_MASK_VBCF) {
          if (verbose) Serial.print("Error: VCO bank calibration failed on channel ");
          Serial.println(channel);
          return 2;  
    }
    return 0;
}

// set the transmitter power on the chip
void A7105_SetPower(int power)
{
    /*
    Power amp is ~+16dBm so:
    TXPOWER_100uW  = -23dBm == PAC=0 TBG=0
    TXPOWER_300uW  = -20dBm == PAC=0 TBG=1
    TXPOWER_1mW    = -16dBm == PAC=0 TBG=2
    TXPOWER_3mW    = -11dBm == PAC=0 TBG=4
    TXPOWER_10mW   = -6dBm  == PAC=1 TBG=5
    TXPOWER_30mW   = 0dBm   == PAC=2 TBG=7
    TXPOWER_100mW  = 1dBm   == PAC=3 TBG=7
    TXPOWER_150mW  = 1dBm   == PAC=3 TBG=7
    */
    u8 pac, tbg;
    switch(power) {
        case 0: pac = 0; tbg = 0; break;
        case 1: pac = 0; tbg = 1; break;
        case 2: pac = 0; tbg = 2; break;
        case 3: pac = 0; tbg = 4; break;
        case 4: pac = 1; tbg = 5; break;
        case 5: pac = 2; tbg = 7; break;
        case 6: pac = 3; tbg = 7; break;
        case 7: pac = 3; tbg = 7; break;
        default: pac = 0; tbg = 0; break;
    };
    A7105_WriteReg(0x28, (pac << 3) | tbg);
}

/******************************************************************************
                         SPI commands on the A7105
 ******************************************************************************
 
 The procedure for transferring data to and from the A7105 over SPI is not entirely obvious,
 so I shall detail it here. The A7105 accepts two types of command, strobe commands and register 
 access commands. Strobe commands are one byte in length, and register access commands are at least
 two bytes in length.
 
 The first byte comprises the opcode and the register address. Composition is as follows:
    
     The leftmost bit flags the command as either a strobe or a register access. 
     1 = strobe, 0 = register access. Note, the strobe commands have this flag pre-raised.

     The second leftmost bit flags a register access command as either read or write. 
     1 = read, 0 = write.
    
     The rest of the bits are simply the register address.
    
 The rest of the bytes of a register access command is for transferring the data. The number of bytes in this section 
 is dictated by the size of the register or buffer being accessed. For further details, see the documentation.
 In a write command, we fill this byte with the data to be written. In a read command, we transmit a null value and 
 allow SPI.transfer() to return the data it finds on the bus. 

 ******************************************************************************/
 
// Transmits the given strobe command. Commands are enumerated in a7105.h and detailed in the documentation
void A7105_Strobe(enum A7105_State state)
{
    // NB: the CS pin must be lowered before and raised after every communication with the chip
    CS_LO();
    SPI.transfer(state);
    CS_HI();
}

// Normal registers, essentially everything except the FIFO buffer and the ID register,
// hold only one byte. These two functions therefore transfer only one byte.

void A7105_WriteReg(u8 address, u8 data)
{
    CS_LO();
    SPI.transfer(address); // spi_xfer(SPI2, address);
    SPI.transfer(data);    // spi_xfer(SPI2, data); 
    CS_HI();
}
u8 A7105_ReadReg(u8 address)
{
    u8 data;
    int i;
    CS_LO();

    // raise the read flag on the address
    SPI.transfer(0x40 | address); 
    data = SPI.transfer(0);
    CS_HI();
    return data;
}
 
 
void A7105_WriteData(u8 *dpbuffer, u8 len, u8 channel)
{
    int i;
    CS_LO();
    SPI.transfer(A7105_RST_WRPTR);    //reset write FIFO PTR
    SPI.transfer(0x05); // FIFO DATA register - about to send data to put into FIFO
    for (i = 0; i < len; i++)
        SPI.transfer(dpbuffer[i]); // send some data
    CS_HI();

    // set the channel
    A7105_WriteReg(0x0F, channel);

    CS_LO();
    SPI.transfer(A7105_TX); // strobe command to actually transmit the data
    CS_HI();
}

void A7105_ReadData(u8 *dpbuffer, u8 len)
{
    A7105_Strobe(A7105_RST_RDPTR);
    /*
    CS_LO();
    SPI.transfer(0x05 | 0x40);
    for(int i = 0; i < len; i++) {
        dpbuffer[i] = SPI.transfer(0);
    }
    CS_HI();
    */
    
    for(int i = 0; i < len; i++) {
        dpbuffer[i] = A7105_ReadReg(0x05);
    }
    return;
}

void A7105_WriteID(unsigned long id)
{
    CS_LO();
    SPI.transfer(0x06);
    SPI.transfer((id >> 24) & 0xFF);
    SPI.transfer((id >> 16) & 0xFF);
    SPI.transfer((id >> 8) & 0xFF);
    SPI.transfer((id >> 0) & 0xFF);
    CS_HI();
}

void A7105_ReadID()
{
    CS_LO();
    SPI.transfer(0x46);
    int result = 0;
    for (int i = 0 ; i < 3 ; i++) {
      result += SPI.transfer(0);
      result = result << 8; 
    }
    result += SPI.transfer(0);
    Serial.println(result);
    CS_HI();
}


// make a distinctive test packet to test that transmission is working correctly
void make_test_packet(u8 testpacket[]) {
    testpacket[0] = 0x00;
    testpacket[1] = 0x11;
    testpacket[2] = 0x22;
    testpacket[3] = 0x33;
    testpacket[4] = 0x44;
    testpacket[5] = 0x55;
    testpacket[6] = 0x66;
    testpacket[7] = 0x77;
    testpacket[8] = 0x88;
    testpacket[9] = 0x99;
    testpacket[10] = 0xaa;
    testpacket[11] = 0xbb;
    testpacket[12] = 0xcc;
    testpacket[13] = 0xdd;
    testpacket[14] = 0xee;
    update_crc();
}

// prnt the contents of packet in a human-readable way
void printpacket(u8 packet[]) {
  int j;
  //Serial.print("Packet received: ");
  if (verbose) {
  for (j = 0 ; j < 16 ; j++) {
      Serial.print(packet[j], HEX);
      Serial.print(" ");
  }}
  Serial.println("");
}

// shouts pockets on the current channel
void A7105_shoutchannel() {
       int i;
       while(1) {
           // build distinctive test packet
           make_test_packet(testpacket);
           // write packet into fifo
           A7105_Strobe(A7105_STANDBY);
           A7105_WriteData(testpacket, 16, channel);
           delayMicroseconds(3000);
           // allow 20 loops for the transmitting flag to clear
           for(i = 0; i< 20; i++) {
               if(! (A7105_ReadReg(A7105_00_MODE) & 0x01))
                   break;
           }
           
        // if not cleared, give message and quit
        if (i == 20) {
            if (verbose) Serial.println("Failed to complete write\n");
            break;
        }
        // success message print - for debugging
        /*else {
            Serial.println("Write successful\n");
            break;         
        }  */
    }
}

void eavesdrop() {
    verbose = true;
    u8 prebind_packet[16];
    int wait_start, wait_end;
    
    Serial.println("Eavesdropping...");
    
    // use findchannel to locate the channel which is currently being broadcast on
    u8 sess_channel = A7105_findchannel();
    
    // strobe to receiver mode, intercept a packet    
    A7105_Strobe(A7105_RX);  
    while(A7105_ReadReg(A7105_00_MODE) & 0x01)
        delayMicroseconds(1);
    A7105_ReadData(prebind_packet, 16);
    
    // use the data from the packet to switch the chip over to the transactions session ID
    A7105_WriteID((prebind_packet[2] << 24) | (prebind_packet[3] << 16) | (prebind_packet[4] << 8) | prebind_packet[5]);
    
    // It is now acceptable to allow the Tx to bind with an Rx
    
    // measure haw long it takes for the next packet to arrive
    wait_start = micros();
    while(true) {
        A7105_Strobe(A7105_RX);  
        while(A7105_ReadReg(A7105_00_MODE) & 0x01) {
            delayMicroseconds(1);
            
            // if it takes more than 5 seconds, we can assume that the session has been terminated
            if ((micros()-wait_start) > 5000000) {
                Serial.println("Session terminated. Rescanning...");
                // ...and therefore start listening for pre-bind packets again
                return;
            }
        }
        
        // record the end time, retrieve the packet
        wait_end = micros();
        A7105_ReadData(receivedpacket, 16);
        
        // print the packet along with the time interval between it and the previous packet
        Serial.print((wait_end - wait_start)/1000);
        
        // start timing for the next packet  
        wait_start = micros();
        Serial.print("us : ");
        printpacket(receivedpacket);
    }
}
    
u8 A7105_findchannel() { 
    int pack_count;
    while (true) {
       for (int i = 0 ; i < no_allowed_channels ; i++) {
          pack_count = 0;
          A7105_WriteReg(A7105_0F_CHANNEL, allowed_ch[i]);
          for (int j = 0 ; j < 20 ; j++)
              pack_count += A7105_sniffchannel();
          if (pack_count > 3) {
              if (verbose) Serial.println("Channel found");
              return allowed_ch[i];
          }
       }
    }
}

// sniffs the currently set channel, prints packets to serial
int A7105_sniffchannel() {
       A7105_Strobe(A7105_RX);  
       delayMicroseconds(3000);
       if(!(A7105_ReadReg(A7105_00_MODE) & 0x01)) {
           A7105_ReadData(receivedpacket, 16);
           printpacket(receivedpacket);
           return 1;
       }
       else
         return 0;
}

// version of sniffchannel which sniffs a channel other than the one which is currently set.
void A7105_sniffchannel(u8 _channel) {
      if (verbose) Serial.print("Switching to channel ");
      Serial.println(_channel);
      Serial.println("");
      A7105_WriteReg(A7105_0F_CHANNEL, _channel);
      while (1) {
          A7105_sniffchannel();
      }
}

// function to sniff a list of channels and see what is being broadcast on them
// attempts sniffing 20 times on each channel before looping, print any results to serial
void A7105_scanchannels(const u8 channels[]) {
    int packetsreceived;
    verbose = true;
    for (int i = 0 ; i < no_allowed_channels ; i++) {
          packetsreceived = 0;
          Serial.println("");
          Serial.print("Switching to channel ");
          Serial.println(channels[i], HEX);
          Serial.println("");
          A7105_WriteReg(A7105_0F_CHANNEL, channels[i]);
          for (int j = 0 ; j < 20 ; j++) {
              packetsreceived += A7105_sniffchannel();
          }
          if (packetsreceived) {
              Serial.print(packetsreceived);
              Serial.print(" packets received on channel ");
              Serial.println(channels[i], HEX);
          }
          else {
              Serial.print("Channel ");
              Serial.print(channels[i], HEX);
              Serial.println(" is clear.");
          }
      }
}

