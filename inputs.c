/* 
 * File:   inputs.c
 * Author: Ian
 * 
 * Handle in input Type variant of the IO. Scan for changes in state and 
 * generate Produced events.
 *
 * Created on 17 April 2017, 13:14
 */

#include <xc.h>
#include "GenericTypeDefs.h"
#include "canmio.h"
#include "mioNv.h"
#include "config.h"
#include "FLiM.h"

extern const NodeVarTable nodeVarTable;
extern Config configs[NUM_IO];
static BYTE debounceAndState[NUM_IO];

/**
 * Initialise the input scan.
 */
void initInputScan(void) {
    for (unsigned char i=0; i<NUM_IO; i++) {
        debounceAndState[i] = 0;
    }
}

/**
 * Called regularly to check for changes on the inputs.
 * TODO: Debounce inputs if required.
 * Generate Produced events upon input change.
 *   
 */
void inputScan(void) {
    // TODO
    for (unsigned char i=0; i< NUM_IO; i++) {
        if (nodeVarTable.moduleNVs.io[i].type == TYPE_INPUT) {
            BYTE input;
            switch(configs[i].port) {
            case 'a':
                input = TRISA & (1<<configs[i].no);
                break;
            case 'b':
                input = TRISB & (1<<configs[i].no);
                break;
            case 'c':
                input = TRISA & (1<<configs[i].no);
                break;
            }
            if (input != debounceAndState[i]) {
                debounceAndState[i] = input;
                if (input) {
                    cbusSendEvent( 0, -1, input, TRUE);
                } else {
                    cbusSendEvent( 0, -1, input, FALSE);
                }
            }
        }
    }
}

