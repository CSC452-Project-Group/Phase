#include <stdio.h>
#include <phase1.h>
#include <phase2.h>
#include "message.h"
#define CLOCK 0
#define DISK 1
#define TERM 3

extern int debugflag2;
extern void disableInterrupts(void);
extern void enableInterrupts(void);
extern void isKernelMode(char *);

int Mbox[7];
int Procblocked = 0;
static int num = 0;
// an error method to handle invalid syscalls 
void nullsys(USLOSS_Sysargs *args)
{
    USLOSS_Console("nullsys(): Invalid syscall %d. Halting...\n", args->number);
    USLOSS_Halt(1);
} /*nullsys */

void clockHandler2(int dev, void *arg)
{
    disableInterrupts();
    isKernelMode("clockHandler2()");
    if (DEBUG2 && debugflag2)
	USLOSS_Console("clockHandler2(): called\n");
    int time = 0;
    int result = USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &time);
    if(result == USLOSS_DEV_INVALID){
	return;
    }
    // check if it is clock device calling
    if(dev != USLOSS_CLOCK_DEV){
	if (DEBUG2 && debugflag2)
            USLOSS_Console("clockHandler2(): called by other device, returning\n");
	    return;
    }

    //static int num = 0;
    num++;
    //int status = 0;
    if(num == 5){
//	int status;
//        USLOSS_DeviceInput(dev, 0, &status);
	MboxCondSend(Mbox[CLOCK], &time, sizeof(int));
       // MboxCondSend(0, &status, 4);
	num = 0;
    }
    if(time-readCurStartTime() >= 100000)
        timeSlice();
    enableInterrupts();

} /* clockHandler */


void diskHandler(int dev, void *arg)
{
    disableInterrupts();
    isKernelMode("clockHandler2()");
    if (DEBUG2 && debugflag2)
	USLOSS_Console("diskHandler(): called\n");

    // check if it is disk device calling
    if(dev != USLOSS_DISK_DEV){
        if (DEBUG2 && debugflag2)
            USLOSS_Console("diskHandler(): called by other device, returning\n")
;
            return;
    }

    int status;
    long unit = (long)arg;
    int check = USLOSS_DeviceInput(dev, unit, &status);

    // check is it is valid
    if(check == USLOSS_DEV_INVALID){
	if (DEBUG2 && debugflag2)
            USLOSS_Console("diskHandler(): unit number invalid, returning\n")
;
            return;
    }

    // Condition send
    MboxCondSend(Mbox[DISK+unit], &status, sizeof(int));
    enableInterrupts();
} /* diskHandler */


void termHandler(int dev, void *arg)
{
    disableInterrupts();
    isKernelMode("termHandler()");
    if (DEBUG2 && debugflag2)
	USLOSS_Console("termHandler(): called\n");

    // check if it is terminal device calling
    if(dev != USLOSS_TERM_DEV){
        if (DEBUG2 && debugflag2)
            USLOSS_Console("termHandler(): called by other device, returning\n")
;
            return;
    }

    int status;
    long unit = (long)arg;
    int check = USLOSS_DeviceInput(dev, unit, &status);

    // check is it is valid
    if(check == USLOSS_DEV_INVALID){
        if (DEBUG2 && debugflag2)
            USLOSS_Console("termHandler(): unit number invalid, returning\n")
;
            return;
    }

    // Condition send
    MboxCondSend(Mbox[TERM+unit], &status, sizeof(int));
    enableInterrupts();
} /* termHandler */


void syscallHandler(int dev, void *arg)
{
    disableInterrupts();
    isKernelMode("syscallHandler()");
    if (DEBUG2 && debugflag2)
	USLOSS_Console("syscallHandler(): called\n");
   
    USLOSS_Sysargs *sys = (USLOSS_Sysargs*) arg;
    if (dev != USLOSS_SYSCALL_INT) {
    if (DEBUG2 && debugflag2) 
      USLOSS_Console("sysCallHandler(): called by other device, returning\n");
    return;
  }

    // check correct system call number
    if (sys->number < 0 || sys->number >= MAXSYSCALLS) {
	USLOSS_Console("syscallHandler(): sys number %d is wrong.  Halting...\n", sys->number);
	USLOSS_Halt(1);
    }

    // nullsys
    nullsys((USLOSS_Sysargs*)arg);
    enableInterrupts();

} /* syscallHandler */

// waitDevice (int, int, int)
int waitDevice(int ID, int unit, int *status){
    disableInterrupts();
    isKernelMode("waitDevice()");
    //USLOSS_Console("waitdevice() called\n");
   // USLOSS_Console("ID: %d, unit: %d\n", ID, unit);
    int check;
    if(ID == USLOSS_CLOCK_DEV){
	check = CLOCK;
    }
    else if(ID == USLOSS_DISK_DEV){
	check = DISK;
    }
    else if(ID == USLOSS_TERM_DEV){
	check = TERM;
    }
    else{
	USLOSS_Console("waitDevice(): Invalid device type; %d. Halting...\n", ID);
        USLOSS_Halt(1);
    }
    //USLOSS_Console("unit: %d\n", unit);
    Procblocked++;
    
    int result = 0;
    // receieve
    result = MboxReceive(Mbox[check+unit], status, sizeof(int));
    //Procblocked--;

    //enableInterrupts();
    //call iszapped to check the return value
    if(result == -3)
	return -1;
    return 0;
    //return isZapped() ? -1 : 0;
} 
