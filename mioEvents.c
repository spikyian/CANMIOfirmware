/*
 * File:   mioEvents.c
 * Author: Ian Hogg
 * 
 * Here we deal with the module specific event handling. This covers:
 * <UL>
 * <LI>Setting of default events.</LI>
 * <li>Processing of inbound, consumed events</LI>
 *</UL>
 * 
 * Created on 10 April 2017, 10:26
 */
#include "mioEvents.h"
#include "mioEEPROM.h"
#include "mioNv.h"
#include "events.h"
#include <stddef.h>

extern void setOutput(unsigned char io, unsigned char state, unsigned char type);

/**
 * Reset events for the IO back to default. Called when the Type lf the IO
 * is changed.
 * @param i the IO number
 */
void defaultEvents(unsigned char i, unsigned char type) {
    WORD nn = ee_read((WORD)EE_NODE_ID);
    WORD en;
    clearEvents(i);
    // add the module's default events for this io

    switch(type) {
        case TYPE_INPUT:
            en=i+1;
            // Produce ACON/ASON and ACOF/ASOF events with en as port number
            doEvlrn(nn, en, 0, ACTION_IO_PRODUCER_INPUT_ON2OFF(i));
            doEvlrn(nn, en, 0, ACTION_IO_PRODUCER_INPUT_OFF2ON(i));
             break;
         
        case TYPE_OUTPUT:
        case TYPE_SERVO:
        case TYPE_BOUNCE:
            en=i+1;
            // Consume ACON/ASON and ACOF/ASOF events with en as port number
            doEvlrn(nn, en, 0, ACTION_IO_CONSUMER_OUTPUT_ON(i));
            doEvlrn(nn, en, 0, ACTION_IO_CONSUMER_OUTPUT_OFF(i));
            break;
        case TYPE_MULTI:
            // no defaults for multi
            break;
    }
}


/**
 * Reset all events back to their default based upon their current Type setting.
 */
void defaultAllEvents(void) {
    // add the module's default events
    unsigned char i;
    for (i=0; i<NUM_IO; i++) {
        defaultEvents(i, nodeVarTable.moduleNVs.io[i].type);
    }
}

/**
 * Clear the events for the IO. Called prior to setting the default events.
 * @param i the IO number
 */
void clearEvents(unsigned char i) {
    unsigned char e;
    for (e=0; e<CONSUMER_ACTIONS_PER_IO; e++) {
        deleteAction(ACTION_IO_CONSUMER_BASE(i)+e);
    }
    for (e=0; e<PRODUCER_ACTIONS_PER_IO; e++) {
        deleteAction(ACTION_IO_PRODUCER_BASE(i)+e);
    }
}

/**
 * Process the consumed events. Perform whatever action is requested and based 
 * upon the Type of IO
 * @param action the required action to be performed.
 * @param msg the full CBUS message so that OPC  and DATA can be retrieved.
 */
void processEvent(BYTE action, BYTE * msg) {
    if (action < NUM_PRODUCER_ACTIONS) return;
    action -= NUM_PRODUCER_ACTIONS;
    unsigned char io = CONSUMER_IO(action);
    action = CONSUMER_ACTION(action);
    unsigned char type = NV->io[io].type;
    setOutput(io, action, type);
}