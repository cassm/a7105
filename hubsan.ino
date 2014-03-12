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

//#include "interface.h"
//#include "mixer.h"
//#include "config/model.h"
//#include <string.h>
//#include <stdlib.h>
#include <Arduino.h>
//#include "config.h"
#include "a7105.h"


volatile s16 Channels[NUM_OUT_CHANNELS];

enum {
    BIND_1,
    BIND_2,
    BIND_3,
    BIND_4,
    BIND_5,
    BIND_6,
    BIND_7,
    BIND_8,
    DATA_1,
    DATA_2,
    DATA_3,
    DATA_4,
    DATA_5,
};
#define WAIT_WRITE 0x80

int cycles = 0;

int hubsan_init()
{
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
    
    if (A7105_calibrate_IF() || A7105_calibrate_VCB(0x00) || A7105_calibrate_VCB(0xA0)) {
        if (verbose) Serial.println("Error: calibration failed");
        return 0;
    }

    //Reset VCO Band calibration
    //A7105_WriteReg(0x25, 0x08);

    A7105_SetPower(TXPOWER_150mW);
    if (verbose) Serial.println("Power: Set 150mW");

    A7105_Strobe(A7105_STANDBY);
    return 1;
}

static void update_crc()
{
    int sum = 0;
    for(int i = 0; i < 15; i++)
        sum += packet[i];
    packet[15] = (256 - (sum % 256)) & 0xff;
}
static void hubsan_build_bind_packet(u8 state)
{
    packet[0] = state;
    packet[1] = channel;
    packet[2] = (sessionid >> 24) & 0xff;
    packet[3] = (sessionid >> 16) & 0xff;
    packet[4] = (sessionid >>  8) & 0xff;
    packet[5] = (sessionid >>  0) & 0xff;
    for (int i = 6 ; i < 15 ; i++)
         packet[i] = 0;
    /*
    packet[6] = 0x08;
    packet[7] = 0xe4; //???
    packet[8] = 0xea;
    packet[9] = 0x9e;
    packet[10] = 0x50;
    packet[11] = (txid >> 24) & 0xff;
    packet[12] = (txid >> 16) & 0xff;
    packet[13] = (txid >>  8) & 0xff;
    packet[14] = (txid >>  0) & 0xff;
    */
    update_crc();
}

s16 get_channel(u8 ch, s32 scale, s32 center, s32 range)
{
  static int a=0;
  if (a++<2550) return 0;
  
//  return 254;
  return 128;
  s32 value = (s32)Channels[ch] * scale / CHAN_MAX_VALUE + center;
    if (value < center - range)
        value = center - range;
    if (value >= center + range)
        value = center + range -1;
    return value;
}


volatile uint8_t throttle=0, rudder=0, aileron = 0, elevator = 0;

static void hubsan_build_packet()
{
    memset(packet, 0, 16);
    //20 00 00 00 80 00 7d 00 84 02 64 db 04 26 79 7b
    packet[0] = 0x20;
    //packet[2] = 0xAA; // test value to try to get the motors to start
    packet[2] = throttle;
    packet[4] = 0xff - rudder; // Rudder is reversed
    packet[6] = 0xff - elevator; // Elevator is reversed
    packet[8] = aileron;
    
    /* - V1 (X4 without LEDs)
    packet[9] = 0x02;
    packet[10] = 0x64;
    packet[11] = (txid >> 24) & 0xff;
    packet[12] = (txid >> 16) & 0xff;
    packet[13] = (txid >>  8) & 0xff;
    packet[14] = (txid >>  0) & 0xff;
    */
    
    // V2 (X4 with LEDs)
    packet[9] = 0x0e; // default: flips on, LEDs on.
    packet[10] = 0x19; 
    
    update_crc();
}

static u16 hubsan_cb()
{
    RED_ON()
    int i, j;
    switch(state) {
    case BIND_1:
    case BIND_3:
    case BIND_5:
    case BIND_7:
        //Serial.println("Clause 1");
        hubsan_build_bind_packet(state == BIND_7 ? 9 : (state == BIND_5 ? 1 : state + 1 - BIND_1));
        A7105_Strobe(A7105_STANDBY);
        A7105_WriteData(packet, 16, channel);
        state |= WAIT_WRITE;
        return 3000;
    case BIND_1 | WAIT_WRITE:
    case BIND_3 | WAIT_WRITE:
    case BIND_5 | WAIT_WRITE:
    case BIND_7 | WAIT_WRITE:
        //Serial.println("Clause 2");
        //wait for completion
        for(i = 0; i< 20; i++) {
          if(! (A7105_ReadReg(A7105_00_MODE) & 0x01))
            break;
        }
        if (i == 20)
            if (verbose) Serial.println("Failed to complete write\n");
        A7105_Strobe(A7105_RX);
        state &= ~WAIT_WRITE;
        state++;
        return 4500; //7.5msec elapsed since last write
    case BIND_2:
    case BIND_4:
    case BIND_6:
        //Serial.println("Clause 3");
        
        if(A7105_ReadReg(A7105_00_MODE) & 0x01) {
            state = BIND_1; //
            if (verbose) Serial.println("Restart");
            return 4500; //No signal, restart binding procedure.  12msec elapsed since last write
        } 

       A7105_ReadData(packet, 16);
       printpacket(packet);
        state++;
        if (state == BIND_5) {
            sessionid = (packet[2] << 24) | (packet[3] << 16) | (packet[4] << 8) | packet[5];
            A7105_WriteID(sessionid);
        }
        return 500;  //8msec elapsed time since last write;
    case BIND_8:
        //Serial.println("Clause 4");
        if(A7105_ReadReg(A7105_00_MODE) & 0x01) {
            state = BIND_7;
            return 15000; //22.5msec elapsed since last write
        }
        A7105_ReadData(packet, 16);
       // test to see what is being received
       printpacket(packet);
        if(packet[1] == 9) {
            RED_OFF();
            BLUE_ON();
            state = DATA_1; // shift to data mode
            A7105_WriteReg(A7105_1F_CODE_I, 0x0F); // enable CRC
            PROTOCOL_SetBindState(0);
            return 28000; //35.5msec elapsed since last write
        } else {
            state = BIND_7;
            return 15000; //22.5 msec elapsed since last write
        }
    case DATA_1:
        //Serial.println("Clause 5");
        //Keep transmit power in sync
        A7105_SetPower(TXPOWER_150mW);
    case DATA_2:
    case DATA_3:
    case DATA_4:
    case DATA_5:
        // surpress the throttle for the first 125 loops. The motors will not start if this does not happen
        if (cycles < 125) {
            if (verbose) Serial.println("Throttle surpressed");
            throttle = 0; 
            cycles++;
        }
        else if (cycles == 125) {
          throttle = 15;
          cycles++;
        }
          
        //Serial.println("Clause 6");
        hubsan_build_packet();
        A7105_WriteData(packet, 16, state == DATA_5 ? channel + 0x23 : channel);
        if (state == DATA_5)
            state = DATA_1;
        else
            state++;
        return 10000;
    }
    return 0;
}

static void initialize() {
    while(1) {
        A7105_Reset();
        if (hubsan_init()) {
          if (verbose) Serial.println("Hubsan_init successful.");
          break;
        }
        else
           if (verbose) Serial.println("Hubsan_init failed.");
    }
    sessionid = rand();
    channel = allowed_ch[rand() % no_allowed_channels];
    state = BIND_1;
}



