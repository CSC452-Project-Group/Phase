#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <stdlib.h> /* needed for atoi() */
#include <driver.h>
#include "providedPrototypes.h"
#include <libuser.h>
#include <stdio.h>
#include <string.h>
/* ---------------------------- Prototypes -------------------------------*/
void sleep(USLOSS_Sysargs *args);
int sleepReal(int seconds);
void diskRead(USLOSS_Sysargs *args);
int diskReadReal(int unit, int track, int first, int sectors, void *buffer);
int  semRunning;

int  ClockDriver(char *);
int  DiskDriver(char *);
int  TermDriver(char *);
int  TermReader(char *);
int  TermWriter(char *);
int diskWriteReal(int, int, int, int, void *);
void diskWrite(USLOSS_Sysargs*);
int diskRW();
int diskSizeReal(int, int*, int*, int*);
void diskSize(USLOSS_Sysargs*);
int termReadReal(int, int, char *);
void termRead(USLOSS_Sysargs*);
int termWriteReal(int, int, char *);
void termWrite(USLOSS_Sysargs*);
void push_clockQueue(procPtr);
void peek_clockQueue();
void add_diskQueue(procPtr);
procPtr peek_diskQueue(int);
void isKernelMode(char *);
void setUserMode();
void initProc(int);
procStruct ProcTable[MAXPROC];
int diskPID[USLOSS_DISK_UNITS]; // pids of the disk drivers
int quickcheck[USLOSS_TERM_UNITS];
procPtr diskQueue[USLOSS_DISK_UNITS];
// mailboxes for terminal device
int charRecvMbox[USLOSS_TERM_UNITS]; // receive char
int charSendMbox[USLOSS_TERM_UNITS]; // send char
int lineReadMbox[USLOSS_TERM_UNITS]; // read line
int lineWriteMbox[USLOSS_TERM_UNITS]; // write line
int pidMbox[USLOSS_TERM_UNITS]; // pid to block
int termInt[USLOSS_TERM_UNITS]; // interupt for term (control writing)
int termPID[USLOSS_TERM_UNITS][3]; // keep track of term procs
procPtr clockQueue;
void
start3(void)
{
    int		i;
    int		clockPID;
    int		pid;
    int		status;
    char        name[128];
    char        termbuf[10];
    /*
     * Check kernel mode here.
     */
    isKernelMode("start3");

    // initialize proc table
    for (i = 0; i < MAXPROC; i++) {
        initProc(i);
	//ProcTable[i].wakeTime = 0;
        //ProcTable[i].blockSem = -1;
    }
    
    // initialize systemCallVec
    systemCallVec[SYS_SLEEP] = sleep;
    systemCallVec[SYS_DISKREAD] = diskRead;
    systemCallVec[SYS_DISKWRITE] = diskWrite;
    systemCallVec[SYS_DISKSIZE] = diskSize;
    systemCallVec[SYS_TERMREAD] = termRead;
    systemCallVec[SYS_TERMWRITE] = termWrite;

    // mboxes for terminal
    for (i = 0; i < USLOSS_TERM_UNITS; i++) {
        charRecvMbox[i] = MboxCreate(1, MAXLINE);
        charSendMbox[i] = MboxCreate(1, MAXLINE);
        lineReadMbox[i] = MboxCreate(10, MAXLINE);
        lineWriteMbox[i] = MboxCreate(10, MAXLINE); 
        pidMbox[i] = MboxCreate(1, sizeof(int));
	quickcheck[i] = 0;
    }
    /*
     * Create clock device driver 
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice.
     */
    semRunning = semcreateReal(0);
    clockPID = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockPID < 0) {
	USLOSS_Console("start3(): Can't create clock driver\n");
	USLOSS_Halt(1);
    }
    /*
     * Wait for the clock driver to start. The idea is that ClockDriver
     * will V the semaphore "semRunning" once it is running.
     */

    sempReal(semRunning);

    /*
     * Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned.
     */

    for (i = 0; i < USLOSS_DISK_UNITS; i++) {
        char temp[10];
        sprintf(temp, "%d", i);
        diskPID[i] = fork1("Disk driver", DiskDriver, temp, USLOSS_MIN_STACK, 2);
        sempReal(semRunning);
        if(diskPID[i] < 0){
          USLOSS_Console("start3(): Can't create disk driver\n");
          USLOSS_Halt(1);

        }
    }


    // May be other stuff to do here before going on to terminal drivers

    /*
     * Create terminal device drivers.
     */
    for (i = 0; i < USLOSS_TERM_UNITS; i++) {
        sprintf(termbuf, "%d", i); 
        termPID[i][0] = fork1(name, TermDriver, termbuf, USLOSS_MIN_STACK, 2);
        termPID[i][1] = fork1(name, TermReader, termbuf, USLOSS_MIN_STACK, 2);
        termPID[i][2] = fork1(name, TermWriter, termbuf, USLOSS_MIN_STACK, 2);
        sempReal(semRunning);
        sempReal(semRunning);
        sempReal(semRunning);
     }

    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case first letters, as shown in provided_prototypes.h
     */
    pid = spawnReal("start4", start4, NULL, 4 * USLOSS_MIN_STACK, 3);
    pid = waitReal(&status);
    /*
     * Zap the device drivers
     */
    status = 0;
    zap(clockPID);
    join(&status);  // clock drive
    for (i = 0; i < USLOSS_DISK_UNITS; i++) {
	semvReal(ProcTable[diskPID[i]].blockSem);
	zap(diskPID[i]);
	join(&status);
    }/*
    semvReal(ProcTable[diskPID[0]].blockSem);
    
    // zap disk driver
    zap(diskPID[0]);
    semvReal(ProcTable[diskPID[1]].blockSem);
    zap(diskPID[1]);
*/
	//dumpProcesses();
    for (i = 0; i < USLOSS_TERM_UNITS; i++) {
        MboxSend(charRecvMbox[i], NULL, 0);
	zap(termPID[i][1]);        
        join(&status);         
    }

    for (i = 0; i < USLOSS_TERM_UNITS; i++) {
        MboxSend(lineWriteMbox[i], NULL, 0);
	zap(termPID[i][2]);
        join(&status);         
     }

    char filename[50];
    for(i = 0; i < USLOSS_TERM_UNITS; i++)
    {
        int ctrl = 0;
        ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
        int result = USLOSS_DeviceOutput(USLOSS_TERM_DEV, i, (void *)((long) ctrl));
        if(result != USLOSS_DEV_OK) {
        	USLOSS_Console("ERROR: USLOSS_TERM_DEV failed! Exiting....'n");
        	USLOSS_Halt(1);
    	}

        // file stuff
        sprintf(filename, "term%d.in", i);
        FILE *f = fopen(filename, "a+");
        fprintf(f, "last line\n");
        fflush(f);
        fclose(f);

        // actual termdriver zap
        zap(termPID[i][0]);
        join(&status);
    }
    // eventually, at the end:
    quit(0);
    
}

/*********************************************************************************/
/* -------------------------------- ClockDriver -------------------------------- */
/*********************************************************************************/

int
ClockDriver(char *arg)
{
    int result;
    int status;

    // Let the parent know we are running and enable interrupts.
    semvReal(semRunning);
    int resultC = USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
    if(resultC != USLOSS_DEV_OK) {
        USLOSS_Console("ERROR: USLOSS_PsrSet failed! Exiting....'n");
        USLOSS_Halt(1);
    }
    // Infinite loop until we are zap'd
    while(! isZapped()) {
		result = waitDevice(USLOSS_CLOCK_DEV, 0, &status);
		if (result != 0) {
			return 0;
		}
		/*
		 * Compute the current time and wake up any processes
		 * whose time has come.
		 */

		//procPtr proc;
		while (clockQueue != NULL && clockQueue->wakeTime < status) {
			//deq4(&sleepQueue);
			//USLOSS_Console("ClockDriver(): Waking up process %d\n", proc->pid);
			//USLOSS_Console("clockDriver(): semaphore %d\n", proc->blockSem);
			semvReal(clockQueue->blockSem);
			peek_clockQueue();
			//USLOSS_Console("ClockDriver(): after semvReal\n");
		}
    }
    return 0;
}

void sleep(USLOSS_Sysargs *args) {

	isKernelMode("sleep");

	int seconds = (long)args->arg1;

	if (isZapped())
		terminateReal(1);

	int result = sleepReal(seconds);

	args->arg4 = (void *)((long)result);

	//USLOSS_Console("sleep(): after wake up\n");

	setUserMode();
}

int sleepReal(int seconds) {

	isKernelMode("sleepReal");
	long status;
	long pid;
	if (seconds < 0)
		return -1;
	else{
	    gettimeofdayReal(&status);
            int wakeupTime = seconds*1000000 + status;
            getPID_real(&pid);
            ProcTable[pid%MAXPROC].wakeTime = wakeupTime;
            if(ProcTable[pid%MAXPROC].blockSem == -1){
                ProcTable[pid%MAXPROC].blockSem = semcreateReal(0);
            }
            push_clockQueue(&ProcTable[pid%MAXPROC]);
            sempReal(ProcTable[pid%MAXPROC].blockSem);
            return 0;

	}
}

/********************************************************************************/
/* -------------------------------- DiskDriver -------------------------------- */
/********************************************************************************/

int
DiskDriver(char *arg)
{
    long pid;
    int unit = atoi( (char *) arg);
    int result, status;

    getPID_real(&pid);

    // initialize
    procPtr driver = &ProcTable[diskPID[unit]];
    driver->track = 0;
    driver->diskRequest.reg1 = (void*)(long)0;
    diskQueue[unit] = NULL;
    driver->blockSem = semcreateReal(0);
    
    semvReal(semRunning);

    // Infinite loop until we are zap'd
    while(1){
        sempReal(driver->blockSem);
        if(isZapped())
            break;

        // get process node from disk queue
        procPtr node = peek_diskQueue(unit);
        if(node->diskRequest.opr == USLOSS_DISK_TRACKS){
            result = USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &(node->diskRequest));
            if (result != USLOSS_DEV_OK) {
                return 0;
            }

            result = waitDevice(USLOSS_DISK_DEV, unit, &status);
            if (result != USLOSS_DEV_OK) {
                return 0;
            } 
        }
        // read and write request
        else{
            driver->track = node->track;
            for(int i = 0; i < node->sectors;){

                // find the track
                driver->diskRequest.opr = USLOSS_DISK_SEEK;
                driver->diskRequest.reg1 = (void*)(long)(driver->track);
                int a = USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &(driver->diskRequest));
                if (a != USLOSS_DEV_OK) {
                    return 0;
                }

                result = waitDevice(USLOSS_DISK_DEV, unit, &status);
                if (result != USLOSS_DEV_OK) {
                    return 0;
                }
                if (driver->track != node->track){
                    node->diskRequest.reg1 = (void*)(long)0;
                }

                // read or wirte sectors
                for(int j = (int)(long)(node->diskRequest.reg1); j < USLOSS_DISK_TRACK_SIZE && i < node->sectors; j++, i++){
                    
                    result = USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &(node->diskRequest));
                    if (result != 0) {
                        return 0;
                    }
                    result = waitDevice(USLOSS_DISK_DEV, unit, &status);
                    if (result != 0) {
                        return 0;
                    }
                    node->diskRequest.reg2 += USLOSS_DISK_SECTOR_SIZE;
                    node->diskRequest.reg1++;
                }
                if (i < node->sectors)
                    driver->track++;
            }
        }
        driver->diskRequest.reg1 = (void*)(long)(((int)(long)(node->diskRequest.reg1) + \
                                    node->sectors) % USLOSS_DISK_SECTOR_SIZE);
        semvReal(node->blockSem);
    }
    return 0;
}

/********************************************************************************/
/* -------------------------------- TermDriver -------------------------------- */
/********************************************************************************/
int
TermDriver(char *arg)
{
    int result;
    int status;
    int unit = atoi( (char *) arg);     // Unit is passed as arg.

    semvReal(semRunning);
    //USLOSS_Console("TermDriver (unit %d): running\n", unit);

    while (!isZapped()) {

        result = waitDevice(USLOSS_TERM_INT, unit, &status);
        if (result != 0) {
            return 0;
        }

        // Try to receive character
        int recv = USLOSS_TERM_STAT_RECV(status);
        if (recv == USLOSS_DEV_BUSY) {
            MboxCondSend(charRecvMbox[unit], &status, sizeof(int));
        }
        else if (recv == USLOSS_DEV_ERROR) {
            USLOSS_Console("TermDriver RECV ERROR\n");
        }

        // Try to send character
        int xmit = USLOSS_TERM_STAT_XMIT(status);
        if (xmit == USLOSS_DEV_READY) {
            MboxCondSend(charSendMbox[unit], &status, sizeof(int));
        }
        else if (xmit == USLOSS_DEV_ERROR) {
            USLOSS_Console("TermDriver XMIT ERROR\n");
        }
    }

    return 0;
} /* TermDriver */

/********************************************************************************/
/* -------------------------------- TermReader -------------------------------- */
/********************************************************************************/
int
TermReader(char *arg)
{
    int unit = atoi( (char *) arg);     // Unit is passed as arg.
    int i;
    int receive; // char to receive
    char line[MAXLINE]; // line being created/read
    int next = 0; // index in line to write char

    for (i = 0; i < MAXLINE; i++) { 
        line[i] = '\0';
    }

    semvReal(semRunning);
    while (!isZapped()) {
        // receieve characters
        MboxReceive(charRecvMbox[unit], &receive, sizeof(int));
        char ch = USLOSS_TERM_STAT_CHAR(receive);
        line[next] = ch;
        next++;

        // receive line
        if (quickcheck[unit] < 10 && (ch == '\n' || next == MAXLINE)) {
            //USLOSS_Console("TermReader (unit %d): line send\n", unit);

            line[next] = '\0'; // end with null
            MboxCondSend(lineReadMbox[unit], line, next);
	    quickcheck[unit]++;
            // reset line
            for (i = 0; i < MAXLINE; i++) {
                line[i] = '\0';
            } 
            next = 0;
        }
    }
    return 0;
} /* TermReader */

/********************************************************************************/
/* -------------------------------- TermWriter -------------------------------- */
/********************************************************************************/
int
TermWriter(char *arg)
{
    int unit = atoi( (char *) arg);     // Unit is passed as arg.
    int size;
    int ctrl = 0;
    int next;
    int status;
    int pdd;
    char line[MAXLINE];

    semvReal(semRunning);
    //USLOSS_Console("TermWriter (unit %d): running\n", unit);

    while (!isZapped()) {
        size = MboxReceive(lineWriteMbox[unit], line, MAXLINE); // get line and size
  	//MboxReceive(termPID[unit], &pdd, sizeof(int));
        if (isZapped())
            break;

        // enable xmit interrupt and receive interrupt
        ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
        int a = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) ((long) ctrl));
	if(a != USLOSS_DEV_OK) {
        	USLOSS_Console("ERROR: USLOSS_TERM_DEV failed! Exiting....'n");
        	USLOSS_Halt(1);
    	}
        // xmit the line
        next = 0;
        while (next < size) {
            MboxReceive(charSendMbox[unit], &status, sizeof(int));

            // xmit the character
            int x = USLOSS_TERM_STAT_XMIT(status);
            if (x == USLOSS_DEV_READY) {
                //USLOSS_Console("%c string %d unit\n", line[next], unit);

                ctrl = 0;
                //ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
                ctrl = USLOSS_TERM_CTRL_CHAR(ctrl, line[next]);
                ctrl = USLOSS_TERM_CTRL_XMIT_CHAR(ctrl);
                ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);

                int b = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) ((long) ctrl));
            	if(b != USLOSS_DEV_OK) {
        	    USLOSS_Console("ERROR: USLOSS_TERM_DEV failed! Exiting....'n");
        	    USLOSS_Halt(1);
    		}	
	    }

            next++;
        }

        // enable receive interrupt
        ctrl = 0;
        if (termInt[unit] == 1) 
            ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
        int b = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) ((long) ctrl));
        if(b != USLOSS_DEV_OK) {
                USLOSS_Console("ERROR: USLOSS_TERM_DEV failed! Exiting....'n");
                USLOSS_Halt(1);
        }
	termInt[unit] = 0;
   	char filename[50];
    	sprintf(filename, "term%d.in", unit);
    	FILE *f = fopen(filename, "a+");
    	fprintf(f, "last line\n");
    	fflush(f);
    	fclose(f);
        int pid; 
        MboxReceive(pidMbox[unit], &pid, sizeof(int));
        semvReal(ProcTable[pid % MAXPROC].blockSem);
        
        
    }

    return 0;
} /* TermWriter */

/********************************************************************************/
/* -------------------------------- diskRead -------------------------------- */
/********************************************************************************/
void
diskRead(USLOSS_Sysargs* args)
{
    isKernelMode("diskRead");

    int status = diskReadReal((long) args->arg5, (long) args->arg3, \
                              (long) args->arg4, (long) args->arg2, args->arg1);
    args->arg1 = (void*)(long)status;
    args->arg4 = status == -1 ? (void*)(long)-1 : (void*)(long)0;
    setUserMode();    
}/* diskRead */

/********************************************************************************/
/* -------------------------------- diskReadReal ------------------------------ */
/********************************************************************************/
int
diskReadReal(int unit, int track, int first, int sectors, void *buffer)
{
    // check kernel mode
    isKernelMode("diskReadReal");
    long pid;

    getPID_real(&pid);

    // initialize the semphore
    if(ProcTable[pid%MAXPROC].blockSem == -1)
        ProcTable[pid%MAXPROC].blockSem = semcreateReal(0);

    // set the value in the struct node
    ProcTable[pid%MAXPROC].unit = unit;
    ProcTable[pid%MAXPROC].track = track;
    ProcTable[pid%MAXPROC].diskRequest.reg1 = (void*)(long)first;
    ProcTable[pid%MAXPROC].sectors = sectors;
    ProcTable[pid%MAXPROC].buffer = buffer;
    ProcTable[pid%MAXPROC].diskRequest.reg2 = buffer;    
    ProcTable[pid%MAXPROC].diskRequest.opr = USLOSS_DISK_READ;

    return diskRW();
} /* diskReadReal */

/********************************************************************************/
/* -------------------------------- diskWrite -------------------------------- */
/********************************************************************************/
void
diskWrite(USLOSS_Sysargs* args)
{
    // check kernel mode
    isKernelMode("diskWrite");
    int status = diskWriteReal((long) args->arg5, (long) args->arg3, \
                              (long) args->arg4, (long) args->arg2, args->arg1);
    args->arg1 = (void*)(long)status;
    args->arg4 = status == -1 ? (void*)(long)-1 : (void*)(long)0;
    setUserMode();
} /* diskWrite */

/********************************************************************************/
/* -------------------------------- diskWriteReal------------------------------ */
/********************************************************************************/
int
diskWriteReal(int unit, int track, int first, int sectors, void *buffer)
{
    // check kernel mode
    isKernelMode("diskWriteReal");

    long pid;
    getPID_real(&pid);

    // initialize the semphore
    if(ProcTable[pid%MAXPROC].blockSem == -1)
        ProcTable[pid%MAXPROC].blockSem = semcreateReal(0);

    // set the value in the struct node
    ProcTable[pid%MAXPROC].unit = unit;
    ProcTable[pid%MAXPROC].track = track;
    ProcTable[pid%MAXPROC].diskRequest.reg1 = (void*)(long)first;
    ProcTable[pid%MAXPROC].sectors = sectors;
    ProcTable[pid%MAXPROC].buffer = buffer;
    ProcTable[pid%MAXPROC].diskRequest.reg2 = buffer;    
    ProcTable[pid%MAXPROC].diskRequest.opr = USLOSS_DISK_WRITE;

    return diskRW();
} /* diskWriteReal */

/********************************************************************************/
/* -------------------------------- diskRorW -------------------------------- */
/********************************************************************************/
int
diskRW()
{
    long pid;
    int status, result;

    getPID_real(&pid);
    procPtr curr = &ProcTable[pid % MAXPROC];

    // check invalid
    if(curr->unit < 0 || curr->unit > 1 || curr->track < 0 || curr->track >= USLOSS_DISK_TRACK_SIZE \
        || (int)(long)(curr->diskRequest.reg1) < 0 || (int)(long)(curr->diskRequest.reg1) >= USLOSS_DISK_TRACK_SIZE \
        || curr->buffer == NULL){
        return -1;
    }

    add_diskQueue(curr);
    semvReal(ProcTable[diskPID[curr->unit]].blockSem);
    sempReal(curr->blockSem);

    result = USLOSS_DeviceInput(USLOSS_DISK_DEV, curr->unit, &status);
    if(result != USLOSS_DEV_OK) {
        USLOSS_Console("diskRW(): USLOSS_TERM_DEV failed! Exiting....\n");
        USLOSS_Halt(1);
    }

    return result;
} /* diskRW */

/********************************************************************************/
/* -------------------------------- diskSize -------------------------------- */
/********************************************************************************/
void diskSize(USLOSS_Sysargs* args) {
    isKernelMode("diskSize");
    int unit = (long) args->arg1;
    int sector, track, disk;
    int retval = diskSizeReal(unit, &sector, &track, &disk);
    args->arg1 = (void *) ((long) sector);
    args->arg2 = (void *) ((long) track);
    args->arg3 = (void *) ((long) disk);
    args->arg4 = (void *) ((long) retval);
    setUserMode();
}


/********************************************************************************/
/* -------------------------------- diskSizeReal ------------------------------ */
/********************************************************************************/
int
diskSizeReal(int unit, int *sector, int *track, int *disk)
{
    // check kernel mode
    isKernelMode("diskSizeReal");
    long pid;
    getPID_real(&pid);
    procPtr driver = &ProcTable[diskPID[unit]];

    // initialize the semphore
    if(ProcTable[pid%MAXPROC].blockSem == -1)
        ProcTable[pid%MAXPROC].blockSem = semcreateReal(0);

    // set the value in the struct node   
    ProcTable[pid%MAXPROC].unit = unit;
    ProcTable[pid%MAXPROC].diskRequest.reg1 = &driver->track;
    ProcTable[pid%MAXPROC].diskRequest.opr = USLOSS_DISK_TRACKS;    

    // check invalid
    if(unit < 0 || unit > 1){
        return -1;
    }
    
    add_diskQueue(&ProcTable[pid%MAXPROC]);
    semvReal(driver->blockSem);
    sempReal(ProcTable[pid%MAXPROC].blockSem);

    *sector = USLOSS_DISK_SECTOR_SIZE;
    *track = USLOSS_DISK_TRACK_SIZE;
    *disk = driver->track;
    return 0;
    
} /* diskSizeReal */

/********************************************************************************/
/* -------------------------------- termRead ---------------------------------- */
/********************************************************************************/
void
termRead(USLOSS_Sysargs* args)
{
    // check kernel mode
    isKernelMode("termRead");
    char *buffer = (char *) args->arg1;
    int size = (long) args->arg2;
    int unit = (long) args->arg3;

    long retval = termReadReal(unit, size, buffer);

    if (retval == -1) {
        args->arg2 = (void *) ((long) retval);
        args->arg4 = (void *) ((long) -1);
    } else {
        args->arg2 = (void *) ((long) retval);
        args->arg4 = (void *) ((long) 0);
    }
    setUserMode();   
} /* termRead */

/********************************************************************************/
/* -------------------------------- termReadReal ------------------------------ */
/********************************************************************************/
int
termReadReal(int unit, int size, char *buffer)
{
    isKernelMode("termReadReal");

    if (unit < 0 || unit > USLOSS_TERM_UNITS - 1 || size <= 0) {
	//USLOSS_Console("Get here?\n");       
	return -1;
    }
    char line[MAXLINE];
    int ctrl = 0;

    //enable term interrupts
    if (termInt[unit] == 0) {
        //USLOSS_Console("termReadReal enable interrupts\n");
        ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
        int a = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) ((long) ctrl));
        if(a != USLOSS_DEV_OK) {
        	USLOSS_Console("ERROR: USLOSS_TERM_DEV failed! Exiting....'n");
        	USLOSS_Halt(1);
    	}
	termInt[unit] = 1;
    }
    //USLOSS_Console("In termReadReal.\n");
    int retval = MboxReceive(lineReadMbox[unit], &line, MAXLINE);

    //USLOSS_Console("termReadReal (unit %d): size %d retval %d \n", unit, size, retval);

    if (retval > size) {
        retval = size;
    }
    memcpy(buffer, line, retval);

    return retval;
} /* termReadReal */

/********************************************************************************/
/* -------------------------------- termWrite -------------------------------- */
/********************************************************************************/
void
termWrite(USLOSS_Sysargs* args)
{
    // check kernel mode
    isKernelMode("termWrite");
    char *text = (char *) args->arg1;
    int size = (long) args->arg2;
    int unit = (long) args->arg3;

    long retval = termWriteReal(unit, size, text);

    if (retval == -1) {
        args->arg2 = (void *) ((long) retval);
        args->arg4 = (void *) ((long) -1);
    } else {
        args->arg2 = (void *) ((long) retval);
        args->arg4 = (void *) ((long) 0);
    }

    setUserMode();
} /* termWrite */

/********************************************************************************/
/* -------------------------------- termWriteReal------------------------------ */
/********************************************************************************/
int
termWriteReal(int unit, int size, char *text)
{
    isKernelMode("termWriteReal");

    if (unit < 0 || unit > USLOSS_TERM_UNITS - 1 || size < 0) {
        return -1;
    }
    //long pid;
    int pid = getpid();
    //getPID_real(&pid);
    MboxSend(pidMbox[unit], &pid, sizeof(int));
    //USLOSS_Console("Gets here?\n");
    MboxSend(lineWriteMbox[unit], text, size);
    //USLOSS_Console("Gets here after?\n");    
    sempReal(ProcTable[pid % MAXPROC].blockSem);
    //USLOSS_Console("Gets end : %d?\n", size);
   // ProcTable[pid % MAXPROC].pid = -1;
    return size;
} /* termWriteReal */


void isKernelMode(char *name)
{
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        USLOSS_Console("%s: called while in user mode, by process %d. Halting...\n", 
             name, getpid());
        USLOSS_Halt(1); 
    }
} 

void setUserMode()
{
   int result = USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE );
    if(result == USLOSS_DEV_INVALID){
	USLOSS_Console("diskHandler(): unit number invalid, returning\n");
            return;
    }
}

/* initializes proc struct */
void initProc(int pid) {
    isKernelMode("initProc()"); 

    int i = pid % MAXPROC;

    ProcTable[i].pid = pid; 
    ProcTable[i].mboxID = MboxCreate(0, 0);
    ProcTable[i].blockSem = semcreateReal(0);
    ProcTable[i].wakeTime = -1;
    ProcTable[i].diskTrack = -1;
    ProcTable[i].nextDiskPtr = NULL;
    ProcTable[i].prevDiskPtr = NULL;
}

/* ------------------------------------------------------------------------
  Functions for the Queue
   ----------------------------------------------------------------------- */

void add_diskQueue(procPtr node){
    //get the disk unit
    int unit = node->unit;

    if (diskQueue[unit] == NULL){
        diskQueue[unit] = node;
        node->nextdiskQueueProc = NULL;
    }
    else{
        // push node accoring to the track number if disk queue is not empty
        procPtr temp = diskQueue[unit];
        if (temp->track > node->track){
            diskQueue[unit] = node;
            node->nextdiskQueueProc = temp;
        }
        else{
            while(temp->nextdiskQueueProc != NULL && temp->nextdiskQueueProc->track <= node->track)
                temp = temp->nextdiskQueueProc;
            node->nextdiskQueueProc = temp->nextdiskQueueProc;
            temp->nextdiskQueueProc = node;
        }
    }
} /* add_diskQueue */

procPtr peek_diskQueue(int unit){
    procPtr queue = diskQueue[unit];
    int track = ProcTable[diskPID[unit]].track;

    if (queue->track >= track){
        diskQueue[unit] = queue->nextdiskQueueProc;
        return queue;
    }
    else{
        while(queue->nextdiskQueueProc != NULL && queue->nextdiskQueueProc->track < track){
            queue = queue->nextdiskQueueProc;
        }
        if (queue->nextdiskQueueProc == NULL){
            procPtr temp = diskQueue[unit];
            diskQueue[unit] = diskQueue[unit]->nextdiskQueueProc;
            return temp;
        }
        else{
            procPtr temp = queue->nextdiskQueueProc;
            queue->nextdiskQueueProc = temp->nextdiskQueueProc;
            return temp;
        }
    }
} /* peek_diskQueue */


void
push_clockQueue(procPtr node)
{
    if(clockQueue == NULL || clockQueue->wakeTime > node->wakeTime){
        procPtr temp = clockQueue;
        clockQueue = node;
        node->nextclockQueueProc = temp;
    }
    else{
        procPtr temp = clockQueue;
        while(temp->nextclockQueueProc != NULL && temp->nextclockQueueProc->wakeTime <= node->wakeTime)
            temp = temp->nextclockQueueProc;
        node->nextclockQueueProc = temp->nextclockQueueProc;
        temp->nextclockQueueProc = node;
    } 
}

void
peek_clockQueue()
{
    if(clockQueue != NULL)
        clockQueue = clockQueue->nextclockQueueProc;
}

