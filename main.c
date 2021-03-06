/*
 * File:   main.c
 * Author: Ian Hogg
 * 
 * This is the main for the Configurable CANMIO module.
 * 
 * Timer usage:
 * TMR0 used in ticktime for symbol times. Used to trigger next set of servo pulses
 * TMR1 Servo outputs 0, 4, 8, 12
 * TMR2 Servo outputs 1, 5, 9, 13
 * TMR3 Servo outputs 2, 6, 10, 14
 * TMR4 Servo outputs 3, 7, 11, 15
 *
 * Created on 10 April 2017, 10:26
 */
/** TODOs
 * Bootloader and handling of OPC_BOOT
 * DONE  change the START_SOD_EVENT for a learned action/event
 * DONE  consumed event processing
 * DONE  validate NV changes
 * DONE  servo outputs
 * DONE  debounce inputs
 * DONE  invert inputs
 * DONE  invert outputs
 * digital output pulse output.c
 * bounce profiles  
 * DONE  multi-position outputs
 * sequence servos servo.c
 * remember output state in EEPROM outputs.c & servo.c
 * Flicker LED on CAN activity can18.c
 * Work out what to do if all CANIDs are taken can18.c
 * Check handling of NERD is correct and produces correct ENRSP events.c
 * Check handling of REVAL events.c
 * Check handling of REQEV events.c
 * DONE  Fix deleteAction events.c
 * More validation of NV values
 * Need more config changes when changing type
 */

/**
 *	The Main CANMIO program supporting configurable I/O.
 */

#include <xc.h>
#include <stddef.h>
#include <pic18f25k80.h>
#include "module.h"
#include "canmio.h"
#include "mioFLiM.h"
#include "config.h"
#include "../CBUSlib/StatusLeds.h"
#include "inputs.h"
#include "mioEEPROM.h"
#include "../CBUSlib/events.h"
#include "mioNv.h"
#include "../CBUSlib/FLiM.h"
#include "../CBUSlib/romops.h"

extern BYTE BlinkLED();
extern void startServos();
extern void initServos();
extern void pollServos();
extern inline void timer1DoneInterruptHandler();
extern inline void timer2DoneInterruptHandler();
extern inline void timer3DoneInterruptHandler();
extern inline void timer4DoneInterruptHandler();

unsigned char canid = 0;        // initialised from ee
unsigned int nn = DEFAULT_NN;   // initialised from ee
//int mode;                       // initialised from ee

// PIN configs
Config configs[NUM_IO] = {
    {18, 'C', 7},   //0
    {17, 'C', 6},   //1
    {16, 'C', 5},   //2
    {15, 'C', 4},   //3
    {14, 'C', 3},   //4
    {13, 'C', 2},   //5
    {12, 'C', 1},   //6
    {11, 'C', 0},   //7
    {21, 'B', 0},   //8
    {22, 'B', 1},   //9
    {25, 'B', 4},   //10
    {26, 'B', 5},   //11
    {3,  'A', 1},   //12
    {2,  'A', 0},   //13
    {5,  'A', 3},   //14
    {7,  'A', 5}    //15
};

// forward declarations
BYTE inputScan(void);
void __init(void);
BOOL checkCBUS( void);
void ISRHigh(void);
void initialise(void);
void configIO(unsigned char io);
void defaultPersistentMemory(void);
void setType(unsigned char i, unsigned char type);
void setOutput(unsigned char i, unsigned char state, unsigned char type);
void sendProducedEvent(unsigned char action, BOOL on);


#ifdef __C18__
void high_irq_errata_fix(void);

/*
 * Interrupt vectors (moved higher when bootloader present)
 */

// High priority interrupt vector

//#ifdef BOOTLOADER_PRESENT
    #pragma code high_vector=0x808
//#else
//    #pragma code high_vector=0x08
//#endif


//void interrupt_at_high_vector(void)

void HIGH_INT_VECT(void)
{
    _asm
        CALL high_irq_errata_fix, 1
    _endasm
}

/*
 * See 18F2480 errata
 */
void high_irq_errata_fix(void) {
    _asm
        POP
        GOTO ISRHigh
    _endasm
}

// low priority interrupt vector

//#ifdef BOOTLOADER_PRESENT
    #pragma code low_vector=0x818
//#else
//    #pragma code low_vector=0x18
//#endif

void LOW_INT_VECT(void)
{
//    _asm GOTO ISRLow _endasm
}
#endif

static TickValue   startTime;
static BOOL        started = FALSE;
static TickValue   lastServoPollTime;
static TickValue   lastServoStartTime;
static unsigned char io;

// MAIN APPLICATION
        
/**
 * It is all run from here.
 * Initialise everything and then loop receiving and processing CAN messages.
 */
#ifdef __C18__
void main(void) {
#else
int main(void) @0x800 {
#endif
    initialise();
    startTime.Val = tickGet();
 
    while (TRUE) {
        // Startup delay for CBUS about 2 seconds to let other modules get powered up - ISR will be running so incoming packets processed
        if (!started && (tickTimeSince(startTime) > (NV->sendSodDelay * HUNDRED_MILI_SECOND) + TWO_SECOND)) {
            started = TRUE;
            if (NV->sendSodDelay > 0) {
                sendProducedEvent(ACTION_SOD, TRUE);
            }
        }
        checkCBUS();    // Consume any CBUS message - display it if not display message mode
        FLiMSWCheck();  // Check FLiM switch for any mode changes
        
        if (started) {
            inputScan();    // Strobe keyboard for button presses
            if (tickTimeSince(lastServoStartTime) > 5*ONE_MILI_SECOND) {
                startServos();  // call every 5ms
                lastServoStartTime.Val = tickGet();
            }
            if (tickTimeSince(lastServoPollTime) > 20*ONE_MILI_SECOND) {
                pollServos();
                lastServoPollTime.Val = tickGet();
            }
        }
        // Check for any flashing status LEDs
        checkFlashing();
     } // main loop
} // main
 

/**
 * The order of initialisation is important.
 */
void initialise(void) {
    // enable the 4x PLL
    OSCTUNEbits.PLLEN = 1;
    
    // Digital I/O - disable analogue
    ANCON0 = 0;
    ANCON1 = 0;
    
    // check if EEPROM is valid
    if (ee_read((WORD)EE_RESET) != 0xCA) {
        // set EEPROM and Flash to default values
        defaultPersistentMemory();
        // set the reset flag to indicate it has been initialised
        ee_write((WORD)EE_RESET, 0xCA);
    }
    canid = ee_read((WORD)EE_CAN_ID);
    nn = ee_read((WORD)EE_NODE_ID);
    
    initTicker();
    // set up io pins based upon type
    // Enable PORT B weak pullups
    INTCON2bits.RBPU = 0;
    // RB bits 0,1,4,5 need pullups
    WPUB = 0x33; 
    for (io=0; io< NUM_IO; io++) {
        configIO(io);
    }
    initInputScan();
    initServos();
    mioFlimInit(); // This will call FLiMinit, which, in turn, calls eventsInit

    // Enable interrupt priority
    RCONbits.IPEN = 1;
    // enable interrupts, all init now done
    ei();
 
    setStatusLed(flimState == fsFLiM);
}    

/**
 * Set up the EEPROM and Flash.
 * Should only get called once on first power up. Initialised EEPROM and Flash.
 */
void defaultPersistentMemory(void) {
    // set EEPROM to default values
    ee_write((WORD)EE_BOOT_FLAG, 0);
    ee_write((WORD)EE_CAN_ID, DEFAULT_CANID);
    ee_write_short((WORD)EE_NODE_ID, DEFAULT_NN); 
    ee_write((WORD)EE_FLIM_MODE, fsSLiM);
    
    // flash is initialised as a constant in mioNv
    // perform other actions based upon type
    unsigned char i;
    for (io=0; io<NUM_IO; io++) {
        //default type is INPUT
        setType(io, TYPE_INPUT);
    }
    flushFlashImage();
}

/**
 * Set the Type of the IO.
 * @param i the IO
 * @param type the new Type
 */
void setType(unsigned char i, unsigned char type) {
    writeFlashImage((BYTE*)(AT_NV+NV_IO_TYPE(i)), type);
    // set to default NVs
    defaultNVs(i, type);
    // set up the default events
    defaultEvents(i, type);
}

/**
 * Check to see if a message has been received on the CBUS and process 
 * it if one has been received.
 * @return true if a message has been received.
 */
BOOL checkCBUS( void ) {
    char    msg[20];

    if (cbusMsgReceived( 0, (BYTE *)msg )) {
        LED2G = BlinkLED( 1 );           // Blink LED on whilst processing messages - to give indication how busy module is
        parseCBUSMsg(msg);               // Process the incoming message
        return TRUE;
    }
    return FALSE;
}


/**
 * Set up an IO based upon the specified type.
 * Set the port to input or output then call setOutput for the currently remembered state.
 * @param i the IO
 */
void configIO(unsigned char i) {
    if (i >= NUM_IO) return;
    // Now actually set it
    switch (configs[i].port) {
        case 'A':
            if (NV->io[i].type == TYPE_INPUT) {
                TRISA |= (1 << configs[i].no);  // input
            } else {
                TRISA &= ~(1 << configs[i].no); // output
                // If this is an output (OUTPUT, SERVO, BOUNCE) set the value to valued saved in EE
                setOutput(i, ee_read((WORD)EE_OP_STATE+i), NV->io[i].type);
            }
            
            break;
        case 'B':
            if (NV->io[i].type == TYPE_INPUT) {
                TRISB |= (1 << configs[i].no);  // input
            } else {
                TRISB &= ~(1 << configs[i].no); // output
                // If this is an output (OUTPUT, SERVO, BOUNCE) set the value to valued saved in EE
                setOutput(i, ee_read((WORD)EE_OP_STATE+i), NV->io[i].type);
            }
            break;
        case 'C':
            if (NV->io[i].type == TYPE_INPUT) {
                TRISC |= (1 << configs[i].no);  // input
            } else {
                TRISC &= ~(1 << configs[i].no); // output
                // If this is an output (OUTPUT, SERVO, BOUNCE) set the value to valued saved in EE
                setOutput(i, ee_read((WORD)EE_OP_STATE+i), NV->io[i].type);
            }
            break;          
    }
}


void sendProducedEvent(unsigned char action, BOOL on) {
    const Event * ev = getProducedEvent(action);
    if (ev != NULL) {
        cbusSendEvent( 0, ev->NN, ev->EN, on );
    }
}

#ifdef __C18__
// C intialisation - declare a copy here so the library version is not used as it may link down in bootloader area

void __init(void)
{
}

// Interrupt service routines
#if defined(__18CXX)
    #pragma interruptlow ISRHigh
    void ISRHigh(void)
#elif defined(__dsPIC30F__) || defined(__dsPIC33F__) || defined(__PIC24F__) || defined(__PIC24FK__) || defined(__PIC24H__)
    void _ISRFAST __attribute__((interrupt, auto_psv)) _INT1Interrupt(void)
#elif defined(__PIC32MX__)
    void __ISR(_EXTERNAL_1_VECTOR, ipl4) _INT1Interrupt(void)
#else
    void _ISRFAST _INT1Interrupt(void) {
#endif
#else 
    void interrupt low_priority low_isr(void) {
#endif
    tickISR();
    canInterruptHandler();
}


void interrupt high_priority high_isr (void)
{
 /* service the servo pulse width timers */
    if (PIR1bits.TMR1IF) {
        timer1DoneInterruptHandler();
        PIR1bits.TMR1IF = 0;
    }
    if (PIR1bits.TMR2IF) {
        timer2DoneInterruptHandler();
        PIR1bits.TMR2IF = 0;
    }
    if (PIR2bits.TMR3IF) {
        timer3DoneInterruptHandler();
        PIR2bits.TMR3IF = 0;
    }
    if (PIR4bits.TMR4IF) {
        timer4DoneInterruptHandler();
        PIR4bits.TMR4IF = 0;
    }
}

