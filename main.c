/*
 * File:   main.c
 * Author: Ian Hogg
 * 
 * This is the main for the Configurable CANMIO module.
 *
 * Created on 10 April 2017, 10:26
 */
/** TODOs
 * Bootloader
 * change the START_SOD_EVENT for a learned action/event
 * consumed event processing
 * validate NV changes
 * servo outputs
 * debounce inputs
 * bounce profiles
 * multi-position outputs
 */

/**
 *	The Main CANMIO program supporting configurable I/O.
 */

#include <xc.h>

#include "module.h"
#include "canmio.h"
#include "mioFLiM.h"
#include "config.h"
#include "StatusLeds.h"
#include "inputs.h"
#include "mioEEPROM.h"
#include "mioEvents.h"
#include "events.h"
#include "mioNv.h"
#include "FLiM.h"
#include "romops.h"

extern BYTE BlinkLED();

// forward declarations
BYTE inputScan(void);

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

// local function prototypes
/*
 */
void __init(void);
BOOL checkCBUS( void);
void ISRHigh(void);
void initialise(void);
void configIO(unsigned char io);
void defaultPersistentMemory(void);
void setType(unsigned char i, unsigned char type);
void setOutput(unsigned char i, unsigned char state, unsigned char type);

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
    TickValue   startTime;
    BOOL        started = FALSE;
  
    initialise();
    startTime.Val = tickGet();
 
    while (TRUE) {
        // Startup delay for CBUS about 2 seconds to let other modules get powered up - ISR will be running so incoming packets processed
        if (!started && (tickTimeSince(startTime) > (NV->sendSodDelay * HUNDRED_MILI_SECOND) + TWO_SECOND)) {
            started = TRUE;
            if (NV->sendSodDelay > 0) {
                sendStartupSod(START_SOD_EVENT);
            }
        }
        checkCBUS();    // Consume any CBUS message - display it if not display message mode
        FLiMSWCheck();  // Check FLiM switch for any mode changes
        // Strobe keyboard for button presses
        if (started) {
            inputScan();
        }
        // Check for any flashing status LEDs
        checkFlashing();
     } // main loop
} // main
 

/**
 * The order of initialisation is important.
 */
void initialise(void) {
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
    
    // set up io pins based upon type
    unsigned char i;
    for (i=0; i< NUM_IO; i++) {
        configIO(i);
    }
    initInputScan();
    mioFlimInit(); // This will call FLiMinit, which, in turn, calls eventsInit


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
    
    unsigned char i;
    for (i=0; i<NUM_IO; i++) {
        //default type is INPUT
        setType(i, TYPE_INPUT);
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
    defaultNVs(i);
    // set up the default events
    defaultEvents(i);
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

/**
 * Set an output to the requested state.
 * TODO At the moment this just handles all types as digital outputs. Need to implement
 * the servo code and bounce too.
 * TODO send produced events.
 * 
 * @param i the IO
 * @param state on/off or position
 * @param type type of output
 */
void setOutput(unsigned char i, unsigned char state, unsigned char type) {
    switch (configs[i].port) {
        case 'A':
            if (state) {
                // set it
                LATA |= (1<<configs[i].no);
            } else {
                // clear it
                LATA &= ~(1<<configs[i].no);
            }
            break;
        case 'B':
            if (state) {
                // set it
                LATB |= (1<<configs[i].no);
            } else {
                // clear it
                LATB &= ~(1<<configs[i].no);
            }
            break;
        case 'C':
            if (state) {
                // set it
                LATC |= (1<<configs[i].no);
            } else {
                // clear it
                LATC &= ~(1<<configs[i].no);
            }
            break;
    }
            
}


void interrupt high_priority high_isr (void)
{
 /* service routine body goes here */
}

