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
#include "../../CBUSlib/GenericTypeDefs.h"
#include "canmio.h"
#include "mioNv.h"
#include "config.h"
#include "../../CBUSlib/FLiM.h"

extern const NodeVarTable nodeVarTable;
extern Config configs[NUM_IO];
/**
 * The current state of the inputs. This may not be the actual read state uas we
 * could still be doing the debounce. Instead this is the currently reported input state.
 */
static BYTE inputState[NUM_IO];
/*
 * Counts the number of cycles since the input changed state.
 */
static BYTE delayCount[NUM_IO];

// forward declarations
BOOL readInput(unsigned char io);

static unsigned char io;

/**
 * Initialise the input scan.
 * Initialise using the current input state so that we don't generate state 
 * change events on power up.
 */
void initInputScan(void) {
    for (io=0; io<NUM_IO; io++) {
        inputState[io] = readInput(io);
    }
}

/**
 * Called regularly to check for changes on the inputs.
 * Generate Produced events upon input change.
 *   
 */
void inputScan(void) {
    for (io=0; io< NUM_IO; io++) {
        if (nodeVarTable.moduleNVs.io[io].type == TYPE_INPUT) {
            BYTE input = readInput(io);
            if (input != inputState[io]) {
                BOOL change = FALSE;
                // check if we have reached the debounce count
                if (inputState[io] && (delayCount[io] == nodeVarTable.moduleNVs.io[io].nv_io.nv_input.input_on_delay)) {
                    change = TRUE;
                }
                if (!inputState[io] && (delayCount[io] == nodeVarTable.moduleNVs.io[io].nv_io.nv_input.input_off_delay)) {
                    change = TRUE;
                }
                if (change) {
                    delayCount[io] = 0;
                    inputState[io] = input;
                    // check if input is inverted
                    if (nodeVarTable.moduleNVs.io[io].nv_io.nv_input.input_inverted) {
                        input = !input;
                    }
                    // send the changed Produced event
                    if (input) {
                        cbusSendEvent( 0, -1, ACTION_IO_PRODUCER_INPUT_OFF2ON(io), TRUE);
                    } else {
                        // check if OFF events are enabled
                        if (nodeVarTable.moduleNVs.io[io].nv_io.nv_input.input_enable_off) {
                            cbusSendEvent( 0, -1, ACTION_IO_PRODUCER_INPUT_ON2OFF(io), FALSE);
                        }
                    }
                }
                delayCount[io]++;
            } else {
                delayCount[io] = 0;
            }
        }
    }
}

/**
 * Read the input state from the IO pins.
 * @param io the IO number
 * @return Non zero is the input is high or FALSE if the input is low
 */
BOOL readInput(unsigned char io) {
    if (nodeVarTable.moduleNVs.io[io].type == TYPE_INPUT) {
            switch(configs[io].port) {
            case 'a':
                return TRISA & (1<<configs[io].no);
            case 'b':
                return TRISB & (1<<configs[io].no);
            case 'c':
                return TRISA & (1<<configs[io].no);
            }
        }
    return FALSE;
}
