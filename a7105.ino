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

#define CS_PIN 10

#define CS_HI() digitalWrite(CS_PIN, HIGH);
#define CS_LO() digitalWrite(CS_PIN, LOW);

u8 packet[16];
u8 channel;
unsigned long sessionid;
const unsigned long txid = 0xdb042679;
u8 state;

void printpacket(u8 packet[]) {
  int j;
  Serial.print("Packet received: ");
  for (j = 0 ; j < 16 ; j++) {
      Serial.print(packet[j], HEX);
      Serial.print(" ");
  }
  Serial.println("");
}

void A7105_WriteReg(u8 address, u8 data)
{
    CS_LO();
    SPI.transfer(address); // spi_xfer(SPI2, address);
    SPI.transfer(data);    // spi_xfer(SPI2, data); 
    CS_HI();
}


void A7105_Setup() {
  pinMode(CS_PIN, OUTPUT);
  SPI.begin();
  SPI.setDataMode(SPI_MODE0);
//  SPI.setClockDivider(21);
  SPI.setBitOrder(MSBFIRST);
  // set gpio1 to SDO (MISO) by writing to reg GIO1S
  A7105_WriteReg(0x0b,0x19); // 0b0110
  Serial.println("Configuration complete.");
}

u8 A7105_ReadReg(u8 address)
{
    u8 data;
    int i;
    CS_LO();
    // Bits A7-A0 make up the first u8.
    // Bit A7 = 1 == Strobe.  A7 = 0 == access register.
    // Bit a6 = 1 == read.  a6 = 0 == write. 
    // bits 0-5 = address.  Assuming address < 64 the below says we want to read an address.
    SPI.transfer(0x40 | address); 
    //spi_xfer(SPI2, 0x40 | address);
    data = SPI.transfer(0);
    CS_HI();
//    Serial.print(address); Serial.print(" "); Serial.println(data);
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
    A7105_Strobe(A7105_RST_RDPTR); //A7105_RST_RDPTR
    for(int i = 0; i < len; i++) {
        dpbuffer[i] = A7105_ReadReg(0x05);
    }
    return;
}

void A7105_Reset()
{
    Serial.println("Resetting...");
    A7105_WriteReg(0x00, 0x00);
    Serial.println("Waiting...");
    delayMicroseconds(100);
    Serial.print("Mode register: ");
    Serial.println(A7105_ReadReg(0x00));
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

void A7105_Strobe(enum A7105_State state)
{
    CS_LO();
    SPI.transfer(state);
    CS_HI();
}


int hubsan_init()
{
    u8 if_calibration1;
    u8 vco_calibration0;
    u8 vco_calibration1;
    //u8 vco_current;

    //set chip ID
    A7105_WriteID(0x55201041);
    //read back chip ID for sanity checking. Works 29/01/14
    //A7105_ReadID();
    
    // 01100011 - enable auto RSSI, auto IF offset, FIFO mode, ADC measurement
    A7105_WriteReg(A7105_01_MODE_CONTROL, 0x63);
    
    // 00001111 - set FIFO length to 16 bytes (easy mode)
    A7105_WriteReg(A7105_03_FIFOI, 0x0f);
    
    // 00000101 - use crystal for timing, set sys clock divider to 2, disable clock generator
    A7105_WriteReg(A7105_0D_CLOCK, 0x05);

    // 00000010 - data rate = Fsyck / 32 / n+1
    A7105_WriteReg(A7105_0E_DATA_RATE, 0x04);
    
    // 00101011 - TX frequency deviation = 186 KHz
    A7105_WriteReg(A7105_15_TX_II, 0x2b);
    
    // 01100010 - disable frequency compensation, disable data invert, bandwidth = 500 KHz, select up-side band
    A7105_WriteReg(A7105_18_RX, 0x62);

    // 10000000 - LNA and mixer gain = 24dB, manual VGA calibrate
    A7105_WriteReg(A7105_19_RX_GAIN_I, 0x80);
    
    // 00001010 - no options here
    A7105_WriteReg(A7105_1C_RX_GAIN_IV, 0x0A);
    
    // 00000111 - reset code register 1. Is this accidental? raise bit 4 to avoid reset and enable crc
    A7105_WriteReg(A7105_1F_CODE_I, 0x07);
    
    // 00010111 - set demodulator default, code error tolerance to 1 bit, preamble pattern detector to 16 bits
    A7105_WriteReg(A7105_20_CODE_II, 0x17);
    
    // 00100111 - demodulator dc level is preamble average val
    A7105_WriteReg(A7105_29_RX_DEM_TEST_I, 0x47);

    // set to standby mode
    A7105_Strobe(A7105_STANDBY);

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
        Serial.print("Error: IF filter calibration has timed out.");
        Serial.println(A7105_ReadReg(0x02));
        return 0;
    }
    
    // read IF calibration status
    if_calibration1 = A7105_ReadReg(A7105_22_IF_CALIB_I);

    // this seems redundant. Is it?
    A7105_ReadReg(A7105_24_VCO_CURCAL);

    // check to see if auto calibration failure flag is set. If so, give error message and abort
    if(if_calibration1 & A7105_MASK_FBCF) {
        Serial.print("Error: IF filter calibration failed.");
        return 0;
    }


    // These commands were commented out in the code when I got it
    
    //VCO Current Calibration
    //A7105_WriteReg(0x24, 0x13); //Recomended calibration from A7105 Datasheet
    //VCO Bank Calibration
    //A7105_WriteReg(0x26, 0x3b); //Recomended limits from A7105 Datasheet
    //VCO Bank Calibrate channel 0?
    
    //Set Channel to 0
    A7105_WriteReg(A7105_0F_CHANNEL, 0);
    
    //Initiate VCO bank calibration. register will auto clear when complete
    A7105_WriteReg(0x02, 2);
    
    // allow 500ms for calibration to complete
    ms = millis();
    while(millis()  - ms < 500) {
        if(! A7105_ReadReg(0x02))
            break;
    }
    
    // if not complete, issue timeout error and abort
    if (millis() - ms >= 500){
          Serial.print("Error: VCO bank calibration timed out. (channel 0x00)");
          return 0;
        }
    
    // if auto calibration fail flag is high, print error and abort
    vco_calibration0 = A7105_ReadReg(A7105_25_VCO_SBCAL_I);
    if (vco_calibration0 & A7105_MASK_VBCF) {
          Serial.print("Error: VCO bank calibration failed. (channel 0x00)");
          return 0;  
    }

    //Calibrate channel 0xa0?
    //set channel to a0. Unsure of the utility of this.
    A7105_WriteReg(A7105_0F_CHANNEL, 0xA0);
    
    //Initiate VCO bank calibration. register will auto clear when complete
    A7105_WriteReg(A7105_02_CALC, 2);
    
    // allow 500ms for calibration to complete
    ms = millis();
    while(millis()  - ms < 500) {
        if(! A7105_ReadReg(A7105_02_CALC))
            break;
    }
    
    // if not complete, issue timeout error and abort
    if (millis() - ms >= 500){
          Serial.print("Error: VCO bank calibration timed out. (channel 0xA0)");
          return 0;
        }
        
    // if auto calibration fail flag is high, print error and abort
    vco_calibration1 = A7105_ReadReg(A7105_25_VCO_SBCAL_I);
    if (vco_calibration1 & A7105_MASK_VBCF) {
          Serial.print("Error: VCO bank calibration failed. (channel 0xA0)");
          return 0;
        }

    //Reset VCO Band calibration
    //A7105_WriteReg(0x25, 0x08);

    A7105_SetPower(TXPOWER_150mW);
    Serial.println("Power: Set 150mW");

    A7105_Strobe(A7105_STANDBY);
    return 1;
}

static void initialize() {

    while(1) {
        if (hubsan_init()) {
          Serial.println("Hubsan_init successful.");
          break;
        }
        else {
          Serial.println("Hubsan_init failed.");
          Serial.println("Re-initialising...");
          A7105_Reset();
          Serial.println("Reset complete.");
          A7105_Setup();
          Serial.println("Setup complete.");
        }
    }
    sessionid = rand();
    channel = hubsan_ch[rand() % sizeof(hubsan_ch)];
    A7105_WriteReg(A7105_0F_CHANNEL, channel);
    
    Serial.print("Channel is set to ");
    Serial.println(channel);
}

static void update_crc()
{
    int sum = 0;
    for(int i = 0; i < 15; i++)
        sum += packet[i];
    packet[15] = (256 - (sum % 256)) & 0xff;
}

void makepacket() {
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
