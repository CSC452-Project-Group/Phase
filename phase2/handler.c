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

// an error method to handle invalid syscalls 
void nullsys(USLOSS_Sysargs *args)
{
    USLOSS_Console("nullsys(): Invalid syscall. Halting...\n");
    USLOSS_Halt(1);
} /*nullsys */

void clockHandler2(int dev, void *arg)
{
    disableInterrupts();
    isKernelMode("clockHandler2()");
    if (DEBUG2 && debugflag2)
	USLOSS_Console("clockHandler2(): called\n");
    
    // check if it is clock device calling
    if(dev != USLOSS_CLOCK_DEV){
	if (DEBUG2 && debugflag2)
            USLOSS_Console("clockHandler2(): called by other device, returning\n");
	    return;
    }

    static int num = 0;
    num++;
    if(num == 5){
	int status;
//        USLOSS_DeviceInput(dev, 0, &status);
	MboxCondSend(Mbox[CLOCK], &status, sizeof(int));
	num = 0;
    }

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

    int check;
    if(ID == USLOSS_CLOCK_DEV){
	check = CLOCK;
    }
    if(ID == USLOSS_DISK_DEV){
	check = DISK;
    }
    if(ID == USLOSS_TERM_DEV){
	check = TERM;
    }
    else{
	USLOSS_Console("waitDevice(): Invalid device type; %d. Halting...\n", ID);
        USLOSS_Halt(1);
    }

    Procblocked++;
    // receieve
    MboxReceive(Mbox[check+unit], status, sizeof(int));
    Procblocked--;

    enableInterrupts();
    //call iszapped to check the return value
    return isZapped() ? -1 : 0;
}
