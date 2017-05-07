/* 
 * File:   mioEvents.h
 * Author: Ian
 *
 * Created on 17 April 2017, 13:14
 */

#ifndef MIOEVENTS_H
#define	MIOEVENTS_H

#ifdef	__cplusplus
extern "C" {
#endif
    /*
     * This is where all the module specific events are defined.
     * The following definitions are required by the FLiM code:
     * NUM_PRODUCER_ACTIONS, NUM_CONSUMER_ACTIONS, HASH_LENGTH, EVT_NUM, 
     * EVperEVT, NUM_CONSUMED_EVENTS, AT_ACTION2EVENT, AT_EVENT2ACTION    
     */
 
#include "canmio.h"
    
#define ACTION_IO_PRODUCER_1                0
#define ACTION_IO_PRODUCER_2                1
#define ACTION_IO_PRODUCER_3                2
#define ACTION_IO_PRODUCER_4                3
#define PRODUCER_ACTIONS_PER_IO             4
#define NUM_PRODUCER_ACTIONS                (NUM_IO * PRODUCER_ACTIONS_PER_IO)
    
#define ACTION_IO_CONSUMER_1                0
#define ACTION_IO_CONSUMER_2                1
#define ACTION_IO_CONSUMER_3                2
#define ACTION_IO_CONSUMER_4                3
#define CONSUMER_ACTIONS_PER_IO             4   
#define NUM_CONSUMER_ACTIONS                (NUM_IO * CONSUMER_ACTIONS_PER_IO)
  
#define NUM_ACTIONS                         (NUM_CONSUMER_ACTIONS + NUM_PRODUCER_ACTIONS)

#define ACTION_IO_PRODUCER_BASE(i)              (PRODUCER_ACTIONS_PER_IO*(i))
#define ACTION_IO_CONSUMER_BASE(i)              (NUM_PRODUCER_ACTIONS+CONSUMER_ACTIONS_PER_IO*(i))
    
#define ACTION_IO_PRODUCER_INPUT_ON2OFF(i)     (ACTION_IO_PRODUCER_BASE(i)+ACTION_IO_PRODUCER_1)
#define ACTION_IO_PRODUCER_INPUT_OFF2ON(i)     (ACTION_IO_PRODUCER_BASE(i)+ACTION_IO_PRODUCER_2)
#define ACTION_IO_CONSUMER_OUTPUT_ON(i)        (NUM_PRODUCER_ACTIONS+ACTION_IO_CONSUMER_BASE(i)+ACTION_IO_PRODUCER_1)
#define ACTION_IO_CONSUMER_OUTPUT_FLASH(i)     (NUM_PRODUCER_ACTIONS+ACTION_IO_CONSUMER_BASE(i)+ACTION_IO_PRODUCER_2)
#define ACTION_IO_CONSUMER_OUTPUT_OFF(i)       (NUM_PRODUCER_ACTIONS+ACTION_IO_CONSUMER_BASE(i)+ACTION_IO_PRODUCER_3)
#define ACTION_IO_PRODUCER_SERVO_OFF(i)        (ACTION_IO_PRODUCER_BASE(i)+ACTION_IO_PRODUCER_1)
#define ACTION_IO_PRODUCER_SERVO_MID(i)        (ACTION_IO_PRODUCER_BASE(i)+ACTION_IO_PRODUCER_2)
#define ACTION_IO_PRODUCER_SERVO_ON(i)         (ACTION_IO_PRODUCER_BASE(i)+ACTION_IO_PRODUCER_3)
#define ACTION_IO_CONSUMER_SERVO_OFF(i)        (NUM_PRODUCER_ACTIONS+ACTION_IO_CONSUMER_BASE(i)+ACTION_IO_PRODUCER_1)
#define ACTION_IO_CONSUMER_SERVO_ON(i)         (NUM_PRODUCER_ACTIONS+ACTION_IO_CONSUMER_BASE(i)+ACTION_IO_PRODUCER_2)
#define ACTION_IO_PRODUCER_BOUNCE_OFF(i)       (ACTION_IO_PRODUCER_BASE(i)+ACTION_IO_PRODUCER_3)
#define ACTION_IO_PRODUCER_BOUNCE_ON(i)        (ACTION_IO_PRODUCER_BASE(i)+ACTION_IO_PRODUCER_2)
#define ACTION_IO_CONSUMER_BOUNCE_OFF(i)       (NUM_PRODUCER_ACTIONS+ACTION_IO_CONSUMER_BASE(i)+ACTION_IO_PRODUCER_1)
#define ACTION_IO_CONSUMER_BOUNCE_ON(i)        (NUM_PRODUCER_ACTIONS+ACTION_IO_CONSUMER_BASE(i)+ACTION_IO_PRODUCER_2)
#define ACTION_IO_PRODUCER_MULTI_AT1(i)        (ACTION_IO_PRODUCER_BASE(i)+ACTION_IO_PRODUCER_1)
#define ACTION_IO_PRODUCER_MULTI_AT2(i)        (ACTION_IO_PRODUCER_BASE(i)+ACTION_IO_PRODUCER_2)
#define ACTION_IO_PRODUCER_MULTI_AT3(i)        (ACTION_IO_PRODUCER_BASE(i)+ACTION_IO_PRODUCER_3)
#define ACTION_IO_PRODUCER_MULTI_AT4(i)        (ACTION_IO_PRODUCER_BASE(i)+ACTION_IO_PRODUCER_4)
#define ACTION_IO_CONSUMER_MULTI_TO1(i)        (NUM_PRODUCER_ACTIONS+ACTION_IO_CONSUMER_BASE(i)+ACTION_IO_PRODUCER_1)
#define ACTION_IO_CONSUMER_MULTI_TO2(i)        (NUM_PRODUCER_ACTIONS+ACTION_IO_CONSUMER_BASE(i)+ACTION_IO_PRODUCER_2)
#define ACTION_IO_CONSUMER_MULTI_TO3(i)        (NUM_PRODUCER_ACTIONS+ACTION_IO_CONSUMER_BASE(i)+ACTION_IO_PRODUCER_3)
#define ACTION_IO_CONSUMER_MULTI_TO4(i)        (NUM_PRODUCER_ACTIONS+ACTION_IO_CONSUMER_BASE(i)+ACTION_IO_PRODUCER_4)
    
#define CONSUMER_ACTION(a)                     (((a)-NUM_PRODUCER_ACTIONS)%CONSUMER_ACTIONS_PER_IO)
#define CONSUMER_IO(a)                         (((a)-NUM_PRODUCER_ACTIONS)/CONSUMER_ACTIONS_PER_IO)


extern void defaultEvents(unsigned char i);
extern void defaultAllEvents(void);
extern void clearEvents(unsigned char i);

// These are chosen so we don't use too much memory 32*20 = 640 bytes.
// Used to size the hash table used to lookup events in the events2actions table.
#define HASH_LENGTH     32
#define CHAIN_LENGTH    20

#define EVT_NUM                 NUM_ACTIONS // Number of events
#define EVperEVT                17          // Event variables per event - just the action
#define NUM_CONSUMED_EVENTS     192         // number of events that can be taught
#define AT_ACTION2EVENT         0x7E80      //(AT_NV - sizeof(Event)*NUM_PRODUCER_ACTIONS) Size=256 bytes
#define AT_EVENT2ACTION         0x6E80      //(AT_ACTION2EVENT - sizeof(Event2Action)*HASH_LENGTH) Size=4096bytes

extern void processEvent(BYTE eventIndex, BYTE* message);

#ifdef	__cplusplus
}
#endif

#endif	/* MIOEVENTS_H */

