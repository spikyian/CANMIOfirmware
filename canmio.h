/* 
 * File:   canmio.h
 * Author: Ian
 * 
 * This file contain general definitions for the CANMIO module.
 *
 * Created on 17 April 2017, 14:02
 */

#ifndef CANMIO_H
#define	CANMIO_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <xc.h> // include processor files - each processor file is guarded.  

#ifdef __XC8__
#define AT_NAME_ADDRESS @0x810
#define AT_PARAM_ADDRESS @0x820
    // XC8 compiler doesn't use rom
#define rom /* NO ROM */
#else
// defined in the linker scripts in the C8 compiler
#endif

/*******************************************************************
 * IO pin configuration
 */
// Number of IO pins
#define NUM_IO 16
// look in mioNv as the IO pin config is stored in NVs
    
/*******************************************************************
 * Module parameters
 */ 
#define MAJOR_VER 	1
#define MINOR_VER 	'a'        // Minor version character
#define BETA        1

#include "../Cbuslib/GenericTypeDefs.h"
#include "../CBUSlib/cbusdefs8n.h"

#define MANU_ID         MANU_MERG
#define MODULE_ID       MTYP_CANMIO
#define MODULE_TYPE     "MIO"
#define MODULE_FLAGS    PF_COMBI+PF_BOOT  // Producer, consumer, boot
#define BUS_TYPE        PB_CAN
#define LOAD_ADDRESS    0x0800      // Need to put in parameter block at compile time, only known to linker so hard code here
#define MNAME_ADDRESS   LOAD_ADDRESS + 0x20 + sizeof(ParamBlock)   // Put module type string above params so checksum can be calculated at compile time
#define START_SOD_EVENT      0x81

// Time delays 
#define CBUS_START_DELAY    TWO_SECOND

#ifdef	__cplusplus
}
#endif

#endif	/* CANMIO_H */

