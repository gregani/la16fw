/*
 * This file is part of the la16fw project.
 *
 * Copyright (C) 2014-2015 Gregor Anich
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "gpif_stuff.h"

#include <delay.h>
#include <eputils.h>
#include <fx2macros.h>
#include <fx2regs.h>
#include <gpif.h>

#include "debug.h"

#define SYNCDELAY SYNCDELAY4


#define FPGA_READ_DATA  (1<<0)  // ctl0

#define GPIF_TRANSACTION_COUNT  256  // 512 bytes

#define EP2FIFOFULL  (EP24FIFOFLGS & (1<<0))
#define EP2FIFOEMPTY  (EP24FIFOFLGS & (1<<1))
#define EP2FULL (EP2468STAT & bmEP2FULL)
#define EP2EMPTY (EP2468STAT & bmEP2EMPTY)


// GPIF Waveform 0: FIFO Read                                                               
//                                                                                         
// Interval     0         1         2         3         4         5         6     Idle (7) 
//          _________ _________ _________ _________ _________ _________ _________ _________
//                                                                                         
// AddrMode Same Val  Same Val  Same Val  Same Val  Same Val  Same Val  Same Val           
// DataMode NO Data   Activate  Activate  Activate  Activate  Activate  Activate           
// NextData SameData  SameData  SameData  SameData  SameData  SameData  SameData           
// Int Trig No Int    No Int    No Int    No Int    No Int    No Int    No Int             
// IF/Wait  IF        IF        Wait 1    Wait 1    Wait 1    Wait 1    Wait 1             
//   Term A FIFOFull  TCXpire                                                              
//   LFunc  OR        AND                                                                  
//   Term B FPGA RDY  TCXpire                                                              
// Branch1  Then 0    ThenIdle                                                             
// Branch0  Else 1    Else 0                                                               
// Re-Exec  Yes       Yes                                                                  
// Sngl/CRC Default   Default   Default   Default   Default   Default   Default            
// READ_N       1         0         0         0         0         0         0         1    
// CTL1         0         0         0         0         0         0         0         0    
// PROG_B       1         1         1         1         1         1         1         1    
// CTL3         0         0         0         0         0         0         0         0    
// CTL4         0         0         0         0         0         0         0         0    
// CTL5         0         0         0         0         0         0         0         0    

static const BYTE __xdata WaveData_FIFORead[32] =     
{                                      
/* LenBr */ 0x81,     0xB8,     0x01,     0x01,     0x01,     0x01,     0x01,     0x07,
/* Opcode*/ 0x01,     0x03,     0x02,     0x02,     0x02,     0x02,     0x02,     0x00,
/* Output*/ 0x05,     0x04,     0x04,     0x04,     0x04,     0x04,     0x04,     0x05,
/* LFun  */ 0x70,     0x2D,     0x00,     0x00,     0x00,     0x00,     0x00,     0x3F,
};
static const BYTE __xdata WaveData_Unused[32] =     
{                                      
/* LenBr */ 0x01,     0x01,     0x01,     0x01,     0x01,     0x01,     0x01,     0x07,
/* Opcode*/ 0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,
/* Output*/ 0x05,     0x05,     0x05,     0x05,     0x05,     0x05,     0x05,     0x05,
/* LFun  */ 0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x3F,
};                     


static BOOL gpif_active = FALSE;


void
gpif_stuff_init()
{
    BYTE i;
    
    gpif_active = FALSE;
    
    /* config gpif */
    IFCONFIG = (1<<7) | /* internal clock */
               (1<<6) | /* 48MHz */
               (1<<5) | /* enable IFCLK output */
               (1<<4) | /* invert IFCLK */
               (0<<3) | /* sync mode */
               (0<<2) | /* don't output gstate */
               (1<<1) | (0<<0); /* gpif (internal master) */
    SYNCDELAY;

    GPIFABORT = 0xff;
    SYNCDELAY;

    //GPIFREADYCFG = (1<<7) | /* INTRDY = 1 */
    //               (1<<6) | /* pass RDY signals through 2 FFs for syncing */
    //               (1<<5); /* use transaction count expiration as RDY5 flag */
    SYNCDELAY;
    GPIFCTLCFG = (1<<4); /* CTL4 = 1 ...? */
    SYNCDELAY;
    GPIFIDLECS = 0; /* DONE = 0, IDLEDRV = 0 */
    SYNCDELAY;
    GPIFIDLECTL = FPGA_READ_DATA;
    SYNCDELAY;
    GPIFWFSELECT = (1<<6) | (1<<4) | (1<<2) | (0<<0); /* fifo read = 0, others = 1 */
    SYNCDELAY;

    FLOWSTATE = 0;
    FLOWLOGIC = 0;
    FLOWEQ0CTL = 0;
    FLOWEQ1CTL = 0;
    FLOWHOLDOFF = 0;
    FLOWSTB = 0;
    FLOWSTBEDGE = 0;
    FLOWSTBHPERIOD = 0;
    SYNCDELAY;

    /* load waveform data */
    AUTOPTRSETUP = 0x07; /* inc both pointers */
    AUTOPTRH2 = 0xe4;
    AUTOPTRL2 = 0x00;
    AUTOPTRH1 = MSB(&WaveData_FIFORead);
    AUTOPTRL1 = LSB(&WaveData_FIFORead);
    for (i = 0; i < 32; i++)
        EXTAUTODAT2 = EXTAUTODAT1;
    AUTOPTRH1 = MSB(&WaveData_Unused);
    AUTOPTRL1 = LSB(&WaveData_Unused);
    for (i = 0; i < 32; i++)
        EXTAUTODAT2 = EXTAUTODAT1;
    AUTOPTRH1 = MSB(&WaveData_Unused);
    AUTOPTRL1 = LSB(&WaveData_Unused);
    for (i = 0; i < 32; i++)
        EXTAUTODAT2 = EXTAUTODAT1;
    AUTOPTRH1 = MSB(&WaveData_Unused);
    AUTOPTRL1 = LSB(&WaveData_Unused);
    for (i = 0; i < 32; i++)
        EXTAUTODAT2 = EXTAUTODAT1;
    
    /* config ep2 */
    EP2CFG = (1<<7) | /* enable ep */
             (1<<6) | /* dir: in */
             (1<<5) | (0<<4) | /* type: bulk */
             (0<<3) | /* 1=1024, 0=512 bytes */
             (0<<1) | (0<<0); /* quad buffered */
    SYNCDELAY;
    //EP2GPIFPFSTOP = (0<<0); /* stop on transaction count */
    EP2GPIFPFSTOP = (1<<0); /* stop on fifo flag */
    EP2GPIFFLGSEL = (1<<1) | (0<<0); /* fifo flag = full flag */
    SYNCDELAY;
    EP2FIFOCFG = bmWORDWIDE | bmAUTOIN;
    SYNCDELAY;
    EP2AUTOINLENH = 0x02; /* 512 bytes */
    SYNCDELAY;
    EP2AUTOINLENL = 0x00;
    SYNCDELAY;

    RESETFIFO(2);
}


void
gpif_stuff_start()
{
    gpif_stuff_abort(); /* reset FIFO */
    
    gpif_set_tc16(1);
    SYNCDELAY;
    gpif_fifo_read(GPIF_EP2);
    SYNCDELAY;
    gpif_active = TRUE;
}


void
gpif_stuff_abort()
{
    //if (!gpif_active)
    //    return;
    
    GPIFABORT = 0xff;
    SYNCDELAY;
    while (!(GPIFTRIG & (1<<7)));
    
    FIFORESET = 0x80;
    SYNCDELAY;
    EP2FIFOCFG &= ~bmAUTOIN;
    SYNCDELAY;
    FIFORESET = 2;
    SYNCDELAY;
    EP2FIFOCFG |= bmAUTOIN;
    SYNCDELAY;
    FIFORESET = 0;
    SYNCDELAY;

    gpif_active = FALSE;
}
