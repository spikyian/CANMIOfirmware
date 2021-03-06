/* 
 * File:   servo.c
 * Author: Ian
 * 
 * Handle the servo outputs. The output signal is a pulse between 1ms and 2ms where the width of the
 * pulse results in the servo moving to an angle. The outputs are driven by Timers to ensure that the
 * pulse width is accurate - although if interrupts are disabled then the width could be longer than
 * intended.
 * Pulses are output approximately every 20ms. Therefore we need more than 1 timer for all 16 possible 
 * servo outputs (16 * 2ms = 32ms which is greater than the 20ms available). A minimum of 2 timers 
 * (each handling 8 servos) is required but if we allow overdrive beyond 2ms then 3 (6 servos) or 
 * 4 (4 servos) timers is better.
 * Here we use 4 timers. Timer1..Timer4.
 * 
 * The timers are driven from Fosc/4 and use a 1:4 prescalar. With a 16MHz resonator and 4x PLL this 
 * equates to a timer increment every 0.25us. We require counts from 1ms to 2ms or 4000 - 8000 timer ticks.
 * We have an 8 bit position value and actually want to allow a bit of overdrive of the servo 0.9ms - 2.1ms.
 * (3600 ticks - 8400 ticks). This gives a range of 4800 ticks over the 8 bit range. Therefore each value
 * of the position is equivalent to 18.75 ticks - let's call it 19. The 3600 ticks at position 0 so to 
 * convert from position to ticks we need to use:
 *    Ticks = 3600 + 19 * position 
 * This is fine for the 16bit Timer1 and Timer3 but the 8 bit timers Timer2 and Timer4 need a bit more work.
 * 
 *
 * Created on 17 April 2017, 13:14
 */
#include <xc.h>
#include "mioNv.h"
#include "mioEvents.h"
#include "../../CBUSlib/FLiM.h"
#include "config.h"
#include "../../CBUSlib/GenericTypeDefs.h"
#include "../../CBUSlib/TickTime.h"

#define POS2TICK_OFFSET         3600    // change this to affect the min pulse width
#define POS2TICK_MULTIPLIER     19      // change this to affect the max pulse width

// forward definitions
void setupTimer1(unsigned char io);
void setupTimer2(unsigned char io);
void setupTimer3(unsigned char io);
void setupTimer4(unsigned char io);

// Externs
extern Config configs[NUM_IO];
extern void sendProducedEvent(unsigned char action, BOOL on);
extern void setOutputPin(unsigned char io, BOOL state);


enum ServoState {
    OFF,            // not generating any pulses
    STOPPED,        // pulse width fixed, reached desired destination
    MOVING          // pulse width changing
} servoState[NUM_IO];
unsigned char currentPos[NUM_IO];
unsigned char targetPos[NUM_IO];
unsigned char speed[NUM_IO];
unsigned char eventFlags[NUM_IO];
#define EVENT_FLAG_ON       1
#define EVENT_FLAG_OFF      2
#define EVENT_FLAG_MID      4
TickValue  ticksWhenStopped[NUM_IO];

static unsigned char block;
static unsigned char timer2Counter; // the High order byte to make T2 16bit
static unsigned char timer4Counter; // the High order byte to make T4 16bit

void initServos() {
    for (unsigned char io=0; io<NUM_IO; io++) {
        servoState[io] = OFF;
        currentPos[io] = targetPos[io] = ee_read(EE_OP_STATE-io);   // restore last known positions
        speed[io] = 0;
    }
    block = 3;
    // initialise the timers for one-shot mode with interrupts and clocked from Fosc/4
    T1GCONbits.TMR1GE = 0;      // gating disabled
    T1CONbits.TMR1CS = 0;       // clock source Fosc/4
    T1CONbits.T1CKPS = 2;       // 1:4 prescalar
    T1CONbits.SOSCEN = 1;       // clock source Fosc
    T1CONbits.RD16 = 1;         // 16bit read/write
    PIE1bits.TMR1IE = 1;        // enable interrupt
    
    T2CONbits.T2CKPS = 1;       // 4x prescalar
                                // only supports Fosc/4 clock source
    T2CONbits.T2OUTPS = 0;      // 1x postscalar - not used as we get the interrupt before postscalar
    PIE1bits.TMR2IE = 1;        // enable interrupt
    
    T3GCONbits.TMR3GE = 0;      // gating disabled
    T3CONbits.TMR3CS = 0;       // clock source Fosc/4
    T3CONbits.T3CKPS = 2;       // 1:4 prescalar
    T3CONbits.SOSCEN = 1;       // clock source Fosc
    T3CONbits.RD16 = 1;         // 16bit read/write
    PIE2bits.TMR3IE = 1;        // enable interrupt
    
    T4CONbits.T4CKPS = 1;       // 4x prescalar
                                // only supports Fosc/4 clock source
    T4CONbits.T4OUTPS = 0;      // 1x postscalar
    PIE4bits.TMR4IE = 1;        // enable interrupt
}
/**
 * This gets called ever approx 5ms so start the next set of servo pulses.
 * Checks that the servo isn't OFF
 * @param io
 */
void startServos() {
    // increment block before calling setup so that block is left as the current block whilst the
    // timers expire
    block++;
    if (block > 3) block = 0;
    if (nodeVarTable.moduleNVs.io[block*4].type == TYPE_SERVO) {
        if (servoState[block*4] != OFF) setupTimer1(block*4);
    }
    if (nodeVarTable.moduleNVs.io[block*4+1].type == TYPE_SERVO) {
        if (servoState[block*4+1] != OFF) setupTimer2(block*4+1);
    }
    if (nodeVarTable.moduleNVs.io[block*4+2].type == TYPE_SERVO) {
        if (servoState[block*4+2] != OFF) setupTimer3(block*4+2);
    }
    if (nodeVarTable.moduleNVs.io[block*4+3].type == TYPE_SERVO) {
        if (servoState[block*4+3] != OFF) setupTimer4(block*4+3);
    }
}

/**
 * The setupTimer start the Timer as a on-shot for the servo output pulse of
 * a width of that required for the required position angle.
 * @param io
 */
void setupTimer1(unsigned char io) {
    TMR1 = -(POS2TICK_OFFSET + POS2TICK_MULTIPLIER * currentPos[io]);     // set the duration. Negative to count up to 0x0000 when it generates overflow interrupt
    // turn on output
    setOutputPin(io, TRUE);
    T1CONbits.TMR1ON = 1;       // enable Timer1
}
void setupTimer2(unsigned char io) {
    TMR2 = 0;                   // start counting at 0
    WORD ticks = POS2TICK_OFFSET + POS2TICK_MULTIPLIER * currentPos[io];
    PR2 = ticks & 0xFF;       // set the duration
    timer2Counter = ticks >> 8;
    // turn on output
    setOutputPin(io, TRUE);
    T2CONbits.TMR2ON =1;        // enable Timer2
}
void setupTimer3(unsigned char io) {
    TMR3 = -(POS2TICK_OFFSET + POS2TICK_MULTIPLIER * currentPos[io]);     // set the duration. Negative to count up to 0x0000 when it generates overflow interrupt
    // turn on output
    setOutputPin(io, TRUE);
    T3CONbits.TMR3ON = 1;       // enable Timer3
}
void setupTimer4(unsigned char io) {
    TMR4 = 0;                   // start counting at 0
    WORD ticks = POS2TICK_OFFSET + POS2TICK_MULTIPLIER * currentPos[io];
    PR4 = ticks & 0xff;       // set the duration
    timer4Counter = ticks >> 8;
    // turn on output
    setOutputPin(io, TRUE);
    T4CONbits.TMR4ON =1;        // enable Timer4
}

/**
 * These TimerDone routines are called when the on-shot timer expires so we
 * disable the timer and turn the output pin off. 
 * Don't recheck IO type here as it shouldn't be necessary and we want to be as quick as possible.
 */
inline void timer1DoneInterruptHandler() {
    T1CONbits.TMR1ON = 0;       // disable Timer1
    setOutputPin(block*4, FALSE);    
}
inline void timer2DoneInterruptHandler() {
    // Is the 16bit counter now at 0?
    if (timer2Counter == 0) {
        // stop counting
        T2CONbits.TMR2ON =0;        // disable Timer2
        setOutputPin(block*4+1, FALSE);  
    } else {
        // keep counting
        timer2Counter--;
    }
}
inline void timer3DoneInterruptHandler() {
    T3CONbits.TMR3ON = 0;       // disable Timer3t
    setOutputPin(block*4+2, FALSE);    
}
inline void timer4DoneInterruptHandler() {
    // Is the 16bit counter now at 0?
    if (timer4Counter == 0) {
        // stop counting
        T4CONbits.TMR4ON =0;        // disable Timer4
        setOutputPin(block*4+3, FALSE);
    } else {
        // keep counting
        timer4Counter--;
    }
}

/**
 * This handles the servo state machine and moves the servo towards the required
 * position and generates the Produced events. Called approx every 20ms i.e. 50 times a second.
 * Therefore to move a servo through 200 positions using a speed of 5 will take just under 1 second.
 */
void pollServos() {
    for (unsigned char io; io<NUM_IO; io++) {
        int midway = (nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_end_pos + 
                    nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_start_pos)/2;
        if (nodeVarTable.moduleNVs.io[io].type == TYPE_SERVO) {
            BOOL beforeMidway=FALSE;
            switch (servoState[io]) {
                case MOVING:
                    if (targetPos[io] > currentPos[io]) {
                        if (currentPos[io] < midway) {
                            beforeMidway = TRUE;
                        }
                        currentPos[io] += speed[io];
                        if (currentPos[io] > targetPos[io]) {
                            currentPos[io] = targetPos[io];                       }
                        if ((eventFlags[io] & EVENT_FLAG_MID) && (currentPos[io] >= midway) && beforeMidway) {
                            // passed through midway point
                            // we send an ACON/ACOF depending upon direction servo was moving
                            // This can then be used to drive frog switching relays
                            sendProducedEvent(ACTION_IO_PRODUCER_SERVO_MID(io), beforeMidway ?TRUE:FALSE);
                        }
                    } else if (targetPos[io] < currentPos[io]) {
                        if (currentPos[io] > midway) {
                            beforeMidway = TRUE;
                        }
                        currentPos[io] -= speed[io];
                        if (currentPos[io] < targetPos[io]) {
                            currentPos[io = targetPos[io]];
                        }
                        if ((eventFlags[io] & EVENT_FLAG_MID) && (currentPos[io] <= midway) && beforeMidway) {
                            // passed through midway point
                            sendProducedEvent(ACTION_IO_PRODUCER_SERVO_MID(io), TRUE);
                        }
                    }
                    if (targetPos[io] == currentPos[io]) {
                        servoState[io] = STOPPED;
                        ticksWhenStopped[io].Val = tickGet();
                        // send ON event or OFF
                        sendProducedEvent(ACTION_IO_PRODUCER_SERVO_ON(io), (eventFlags[io]&EVENT_FLAG_ON) ? TRUE : FALSE);
                    }
                    break;
                case STOPPED:
                    // if we have been stopped for more than 1 sec then change to OFF
                    if (tickTimeSince(ticksWhenStopped[io]) > ONE_SECOND) {
                        servoState[io] = OFF;
                    }
                case OFF:
                    // output off
                    // no need to do anything since if output is OFF we don't start the timer in startServos
                    break;
            }
        }
    }
}


/**
 * Set a servo moving to the required state.
 * Handles inverted outputs and generates Produced events.
 * 
 * @param io
 * @param action
 */
void setServoOutput(unsigned char io, unsigned char action) {
    switch (action) {
        case ACTION_IO_CONSUMER_1:  // SERVO OFF
            targetPos[io] = nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_start_pos;
            speed[io] = nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_es_speed;
            eventFlags[io] = EVENT_FLAG_OFF & EVENT_FLAG_MID;
            servoState[io] = MOVING;
            break;
        case ACTION_IO_CONSUMER_2:  // SERVO ON
            targetPos[io] = nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_end_pos;
            speed[io] = nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_se_speed;
            eventFlags[io] = EVENT_FLAG_ON & EVENT_FLAG_MID;
            servoState[io] = MOVING;
            break;
    }
}

/**
 * Set a servo output to the required state, producing a bounce at the OFF end.
 * Handles inverted outputs and generates Produced events.
 * 
 * @param io
 * @param action
 */
void setBounceOutput(unsigned char io, unsigned char action) {
    // TODO Bounce output type
}

/**
 * Sets a servo multi-position output. generates produced events.
 * @param io
 * @param action
 */
void setMultiOutput(unsigned char io, unsigned char action) {
    switch (action) {
        case ACTION_IO_CONSUMER_1:  // SERVO Position 1
            targetPos[io] = nodeVarTable.moduleNVs.io[io].nv_io.nv_multi.multi_pos1;
            speed[io] = nodeVarTable.moduleNVs.servo_speed;
            eventFlags[io] = EVENT_FLAG_ON;
            servoState[io] = MOVING;
            break;
        case ACTION_IO_CONSUMER_2:  // SERVO Position 2
            targetPos[io] = nodeVarTable.moduleNVs.io[io].nv_io.nv_multi.multi_pos2;
            speed[io] = nodeVarTable.moduleNVs.servo_speed;
            eventFlags[io] = EVENT_FLAG_ON;
            servoState[io] = MOVING;
            break;
        case ACTION_IO_CONSUMER_3:  // SERVO Position 3
            if (nodeVarTable.moduleNVs.io[io].nv_io.nv_multi.multi_num_pos >= 3) {
                targetPos[io] = nodeVarTable.moduleNVs.io[io].nv_io.nv_multi.multi_pos3;
                speed[io] = nodeVarTable.moduleNVs.servo_speed;
                eventFlags[io] = EVENT_FLAG_ON;
                servoState[io] = MOVING;
            }
            break;
        case ACTION_IO_CONSUMER_4:  // SERVO Position 4
            if (nodeVarTable.moduleNVs.io[io].nv_io.nv_multi.multi_num_pos >= 4) {
                targetPos[io] = nodeVarTable.moduleNVs.io[io].nv_io.nv_multi.multi_pos4;
                speed[io] = nodeVarTable.moduleNVs.servo_speed;
                eventFlags[io] = EVENT_FLAG_ON;
                servoState[io] = MOVING;
            }
            break;
    }
}


