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
 * of the position is equivalent to 14.0625 ticks - let's call it 14. The 3600 ticks at position 0 so to 
 * convert from position to ticks we need to use:
 *    Ticks = 3600 + 14 * position 
 * This is fine for the 16bit Timer1 and Timer3 but the 8 bit timers Timer2 and Timer4 need a bit more work.
 * 
 *
 * Created on 17 April 2017, 13:14
 */
#include <xc.h>
#include "mioNv.h"
#include "mioEvents.h"
#include "FLiM.h"
#include "config.h"
#include "GenericTypeDefs.h"
#include "TickTime.h"

#define POS2TICK_OFFSET         3600
#define POS2TICK_MULTIPLIER     14

// forward definitions
void setupTimer1(unsigned char io);
void setupTimer2(unsigned char io);
void setupTimer3(unsigned char io);
void setupTimer4(unsigned char io);

// Externs
extern Config configs[NUM_IO];
extern void sendProducedEvent(unsigned char action, BOOL on);
extern void setOutputPin(unsigned char io, BOOL state);

// future state changes
struct {
    DWORD when;
    BOOL state;
    BOOL active;
} futures[NUM_IO];

enum ServoState {
    OFF, STOPPED, MOVING2ON, MOVING2OFF
} servoState[NUM_IO];
unsigned char currentPos[NUM_IO];

static unsigned char block;
static unsigned char timer2Counter; // the High order byte to make T2 16bit
static unsigned char timer4Counter; // the High order byte to make T4 16bit

void initServos() {
    for (unsigned char io=0; io<NUM_IO; io++) {
        servoState[io] = OFF;
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
 * This gets called ever approx 5ms so start the next set of servos.
 * @param io
 */
void startServos() {
    // increment block before calling setup so that block is left as the current block whilst the
    // timers expire
    block++;
    if (block > 3) block = 0;
    if (nodeVarTable.moduleNVs.io[block*4].type == TYPE_SERVO) setupTimer1(block*4);
    if (nodeVarTable.moduleNVs.io[block*4+1].type == TYPE_SERVO) setupTimer2(block*4+1);
    if (nodeVarTable.moduleNVs.io[block*4+2].type == TYPE_SERVO) setupTimer3(block*4+2);
    if (nodeVarTable.moduleNVs.io[block*4+3].type == TYPE_SERVO) setupTimer4(block*4+3);
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
 * position and generates the Produced events.
 */
void pollServos() {
    for (unsigned char io; io<NUM_IO; io++) {
        int midway = (nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_end_pos + 
                    nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_start_pos)/2;
        if (nodeVarTable.moduleNVs.io[io].type == TYPE_SERVO) {
            BOOL beforeMidway=FALSE;
            switch (servoState[io]) {
                case MOVING2ON:
                    if (nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_end_pos > currentPos[io]) {
                        if (currentPos[io] < midway) {
                            beforeMidway = TRUE;
                        }
                        currentPos[io] += nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_se_speed;
                        if (currentPos[io] > nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_end_pos) {
                            currentPos[io = nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_end_pos];
                        }
                        if ((currentPos[io] >= midway) && beforeMidway) {
                            // passed through midway point
                            sendProducedEvent(ACTION_IO_PRODUCER_SERVO_MID(io), TRUE);
                        }
                    }
                    if (nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_end_pos < currentPos[io]) {
                        if (currentPos[io] > midway) {
                            beforeMidway = TRUE;
                        }
                        currentPos[io] -= nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_se_speed;
                        if (currentPos[io] < nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_end_pos) {
                            currentPos[io = nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_end_pos];
                        }
                        if ((currentPos[io] <= midway) && beforeMidway) {
                            // passed through midway point
                            sendProducedEvent(ACTION_IO_PRODUCER_SERVO_MID(io), TRUE);
                        }
                    }
                    if (nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_end_pos == currentPos[io]) {
                        servoState[io] = STOPPED;
                        // send ON event
                        sendProducedEvent(ACTION_IO_PRODUCER_SERVO_ON(io), TRUE);
                    }
                    break;
                case MOVING2OFF:
                    if (nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_start_pos > currentPos[io]) {
                        if (currentPos[io] < midway) {
                            beforeMidway = TRUE;
                        }
                        currentPos[io] += nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_es_speed;
                        if (currentPos[io] > nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_start_pos) {
                            currentPos[io = nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_start_pos];
                        }
                        if ((currentPos[io] >= midway) && beforeMidway) {
                            // passed through midway point
                            sendProducedEvent(ACTION_IO_PRODUCER_SERVO_MID(io), TRUE);
                        }
                    }
                    if (nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_start_pos < currentPos[io]) {
                        if (currentPos[io] > midway) {
                            beforeMidway = TRUE;
                        }
                        currentPos[io] -= nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_es_speed;
                        if (currentPos[io] < nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_start_pos) {
                            currentPos[io = nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_start_pos];
                        }
                        if ((currentPos[io] <= midway) && beforeMidway) {
                            // passed through midway point
                            sendProducedEvent(ACTION_IO_PRODUCER_SERVO_MID(io), TRUE);
                        }
                    }
                    if (nodeVarTable.moduleNVs.io[io].nv_io.nv_servo.servo_start_pos == currentPos[io]) {
                        servoState[io] = STOPPED;
                        // send OFF event
                        sendProducedEvent(ACTION_IO_PRODUCER_SERVO_OFF(io), FALSE);
                    }
                    break;
                case STOPPED:
                    // if we don't already have an OFF scheduled then schedule a change to OFF in 1 second
                case OFF:
                // output off
                    break;
            }
        }
    }
}


/**
 * Set a servo output to the required state.
 * Handles inverted outputs and generates Produced events.
 * 
 * @param io
 * @param action
 */
void setServoOutput(unsigned char io, unsigned char action) {
    // TODO
}

/**
 * Set a servo output to the required state, producing a bounce at the OFF end.
 * Handles inverted outputs and generates Produced events.
 * 
 * @param io
 * @param action
 */
void setBounceOutput(unsigned char io, unsigned char action) {
    // TODO
}

/**
 * Sets a servo multi-position output. generates produced events.
 * @param io
 * @param action
 */
void setMultiOutput(unsigned char io, unsigned char action) {
    // TODO
}


