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
  Serial.println("Configuration complete.");
}

// reset the chip
void A7105_Reset()
{
    // this writes a null value to register 0x00, which triggers the reset
    A7105_WriteReg(0x00, 0x00);
    delayMicroseconds(100);
    Serial.println("Reset complete");
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

// Transmits the given strobe command. Commands are enumerated in a7105.h and detailed in the documentation
void A7105_Strobe(enum A7105_State state)
{
    // NB: the CS pin must be lowered before and raised after every communication with the chip
    CS_LO();
    SPI.transfer(state);
    CS_HI();
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
    SPI.transfer(A7105_TX); // strobe command to actually transmit the daat
    CS_HI();
}

void A7105_ReadData(u8 *dpbuffer, u8 len)
{
    A7105_Strobe(A7105_RST_RDPTR);
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
void make_test_packet() {
    packet[0] = 0x00;
    packet[1] = 0x11;
    packet[2] = 0x22;
    packet[3] = 0x33;
    packet[4] = 0x44;
    packet[5] = 0x55;
    packet[6] = 0x66;
    packet[7] = 0x77;
    packet[8] = 0x88;
    packet[9] = 0x99;
    packet[10] = 0xaa;
    packet[11] = 0xbb;
    packet[12] = 0xcc;
    packet[13] = 0xdd;
    packet[14] = 0xee;
    update_crc();
}

// prnt the contents of packet in a human-readable way
void printpacket(u8 packet[]) {
  int j;
  Serial.print("Packet received: ");
  for (j = 0 ; j < 16 ; j++) {
      Serial.print(packet[j], HEX);
      Serial.print(" ");
  }
  Serial.println("");
}

// shouts pockets on the current channel
void A7105_shoutchannel() {
       int i;
       make_test_packet();
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
        // success message print - for debugging
        /*else {
            Serial.println("Write successful\n");
            break;         
        }  */
    }
}

// sniffs the currently set channel, prints packets to serial
int A7105_sniffchannel() {
       A7105_Strobe(A7105_RX);  
       delayMicroseconds(3000);
       if(A7105_ReadReg(A7105_00_MODE) & 0x01) {
           A7105_ReadData(packet, 16);
           printpacket(packet);
           return 1;
       }
       else
         return 0;
}

// version of sniffchannel which sniffs a channel other than the one which is currently set.
void A7105_sniffchannel(u8 _channel) {
      Serial.print("Switching to channel ");
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
    for (int i = 0 ; i < sizeof(channels) ; i++) {
          packetsreceived = 0;
          Serial.println("");
          Serial.print("Switching to channel ");
          Serial.println(channels[i]);
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

