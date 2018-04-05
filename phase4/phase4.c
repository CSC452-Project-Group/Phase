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
void enqueueSleeper(procPtr p);

/* ---------------------------- Globals -------------------------------*/
int  semRunning;

int  ClockDriver(char *);
int  DiskDriver(char *);
int  TermDriver(char *);
int  TermReader(char *);
int  TermWriter(char *);
int diskWriteReal(int, int, int, int, void *);
void diskWrite(USLOSS_Sysargs*);
int diskReadOrWrite();
int diskSizeReal(int, int*, int*, int*);
void diskSize(USLOSS_Sysargs*);
int termReadReal(int, int, char *);
void termRead(USLOSS_Sysargs*);
int termWriteReal(int, int, char *);
void termWrite(USLOSS_Sysargs*);

void isKernelMode(char *);
void setUserMode();
void initProc(int);
void initDiskQueue(diskQueue*);
void addDiskQ(diskQueue*, procPtr);
procPtr peekDiskQ(diskQueue*);
procPtr removeDiskQ(diskQueue*);

/* Globals */
procStruct ProcTable[MAXPROC];

int diskZapped; // indicates if the disk drivers are 'zapped' or not
diskQueue diskQs[USLOSS_DISK_UNITS]; // queues for disk drivers
diskQueue sleepQueue; // queue for the sleeping user proccesses
int diskPids[USLOSS_DISK_UNITS]; // pids of the disk drivers

// mailboxes for terminal device
int charRecvMbox[USLOSS_TERM_UNITS]; // receive char
int charSendMbox[USLOSS_TERM_UNITS]; // send char
int lineReadMbox[USLOSS_TERM_UNITS]; // read line
int lineWriteMbox[USLOSS_TERM_UNITS]; // write line
int pidMbox[USLOSS_TERM_UNITS]; // pid to block
int termInt[USLOSS_TERM_UNITS]; // interupt for term (control writing)
int termProcTable[USLOSS_TERM_UNITS][3]; // keep track of term procs

void
start3(void)
{
    char	name[128];
    char        termbuf[10];
    char        diskbuf[10];
    int		i;
    int		clockPID;
    int		pid;
    int		status;
    /*
     * Check kernel mode here.
     */
    isKernelMode("start3");

    // initialize proc table
    for (i = 0; i < MAXPROC; i++) {
        initProc(i);
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
    }

	initDiskQueue(&sleepQueue); // initialize the sleep queue

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

    int temp;
    for (i = 0; i < USLOSS_DISK_UNITS; i++) {
        sprintf(diskbuf, "%d", i);
        pid = fork1("Disk driver", DiskDriver, diskbuf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create disk driver %d\n", i);
            USLOSS_Halt(1);
        }

        diskPids[i] = pid;
        sempReal(semRunning); // wait for driver to start running

        //TODO: get number of tracks, implement diskSizeReal
        diskSizeReal(i, &temp, &temp, &ProcTable[pid % MAXPROC].diskTrack);
    }


    // May be other stuff to do here before going on to terminal drivers

    /*
     * Create terminal device drivers.
     */


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
    zap(clockPID);  // clock driver

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

		procPtr proc;
		while (peekDiskQ(&sleepQueue)->wakeTime < status) {
			proc = removeDiskQ(&sleepQueue);
			USLOSS_Console("ClockDriver: Waking up process %d\n", proc->pid);
			semvReal(proc->blockSem);
		}
    }
    return 0;
}

int Sleep(int seconds) {

	USLOSS_Sysargs sysArg;

	//CHECKMODE;
	sysArg.number = SYS_SLEEP;
	sysArg.arg1 = (void *)((long)seconds);

	USLOSS_Syscall(&sysArg);

	return (int)((long)sysArg.arg4);
}

void sleep(USLOSS_Sysargs *args) {

	isKernelMode("sleep");

	int seconds = (int)((long)args->arg1);

	if (isZapped())
		terminateReal(1);

	int result = sleepReal(seconds);

	args->arg4 = (void *)((long)result);

	setUserMode();
}

int sleepReal(int seconds) {

	isKernelMode("sleepReal");

	if (seconds < 0)
		return ERR_INVALID;

	int status;
	int timeNow = waitDevice(USLOSS_CLOCK_DEV, 0, &status);
	int wakeTime = timeNow + seconds;
	int pid = getpid();


	procPtr proc = &ProcTable[pid];
	proc->wakeTime = wakeTime;
	enqueueSleeper(proc);

	return ERR_OK;
}

/********************************************************************************/
/* -------------------------------- DiskDriver -------------------------------- */
/********************************************************************************/

int
DiskDriver(char *arg)
{
    int result;
    int status;
    int unit = atoi( (char *) arg);     // Unit is passed as arg.

    // set up the proc table
    initProc(getpid());
    procPtr me = &ProcTable[getpid() % MAXPROC];
    initDiskQueue(&diskQs[unit]);

    USLOSS_Console("DiskDriver: unit %d started, pid = %d\n", unit, me->pid);

    // Let the parent know we are running and enable interrupts.
    semvReal(semRunning);
    int Result = USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
    if(Result == USLOSS_DEV_INVALID){
	USLOSS_Console("DiskDriver(): unit number invalid, returning\n");
    	return 0;
    }
    // Infinite loop until we are zap'd
    while(!isZapped()) {
        // block on sem until we get request
        sempReal(me->blockSem);
        USLOSS_Console("DiskDriver: unit %d unblocked, zapped = %d, queue size = %d\n", unit, isZapped(), diskQs[unit].size);
        
        if (isZapped()){
            return 0;
	}
        // get request off queue
        if (diskQs[unit].size > 0) {
            procPtr proc = peekDiskQ(&diskQs[unit]);
            int track = proc->diskTrack;

            USLOSS_Console("DiskDriver: taking request from pid %d, track %d\n", proc->pid, proc->diskTrack);
            

            // handle tracks request
            if (proc->diskRequest.opr == USLOSS_DISK_TRACKS) {
                int check = USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &proc->diskRequest);
                if(check == USLOSS_DEV_INVALID){
		    USLOSS_Console("DiskDirver(): check invalid, returning\n");
		    return 0;
		}
		result = waitDevice(USLOSS_DISK_DEV, unit, &status);
                if (result != 0) {
                    return 0;
                }
            }

            else { // handle read/write requests
                while (proc->diskSectors > 0) {
                    USLOSS_DeviceRequest request;
                    request.opr = USLOSS_DISK_SEEK;
                    request.reg1 = &track;
                    int Check = USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &request);
                    if(Check == USLOSS_DEV_INVALID){
                    USLOSS_Console("DiskDirver(): Check invalid, returning\n");
                    return 0;
                }
		    result = waitDevice(USLOSS_DISK_DEV, unit, &status);
                    if (result != 0) {
                        return 0;
                    }

                    
                    USLOSS_Console("DiskDriver: seeked to track %d, status = %d, result = %d\n", track, status, result);

                    // read/write the sectors
                    int sec;
                    for (sec = proc->diskFirstSec; proc->diskSectors > 0 && sec < USLOSS_DISK_TRACK_SIZE; sec++) {
                        proc->diskRequest.reg1 = (void *) ((long) sec);
                        int co = USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &proc->diskRequest);
                        if(co == USLOSS_DEV_INVALID){
                    	    USLOSS_Console("DiskDirver(): co invalid, returning\n");
                    	    return 0;
                }
			result = waitDevice(USLOSS_DISK_DEV, unit, &status);
                        if (result != 0) {
                            return 0;
                        }

                        USLOSS_Console("DiskDriver: read/wrote sector %d, status = %d, result = %d, buffer = %s\n", sec, status, result, proc->diskRequest.reg2);
                        

                        proc->diskSectors--;
                        proc->diskRequest.reg2 += USLOSS_DISK_SECTOR_SIZE;
                    }

                    // request first sector of next track
                    track++;
                    proc->diskFirstSec = 0;
                }
            }

            USLOSS_Console("DiskDriver: finished request from pid %d\n", proc->pid, result, status);

            removeDiskQ(&diskQs[unit]);
            semvReal(proc->blockSem);
        }

    }

    semvReal(semRunning); // unblock parent
    return 0;
}


/* ------------------------------------------------------------------------
   Name - TermDriver
   Purpose -
   Parameters -
   Returns -
   Side Effects -
   ----------------------------------------------------------------------- */
int
TermDriver(char *arg)
{
    return 0;
} /* TermDriver */

/* ------------------------------------------------------------------------
   Name - TermReader
   Purpose -
   Parameters -
   Returns -
   Side Effects -
   ----------------------------------------------------------------------- */
int
TermReader(char *arg)
{
    return 0;
} /* TermReader */

/* ------------------------------------------------------------------------
   Name - TermWriter
   Purpose -
   Parameters -
   Returns -
   Side Effects -
   ----------------------------------------------------------------------- */
int
TermWriter(char *arg)
{
    return 0;
} /* TermWriter */

/* ------------------------------------------------------------------------
   Name - diskRead
   Purpose - Reads one or more sectors from a disk.
   Parameters - arg1: the memory address to which to transfer
                arg2: number of sectors to read
                arg3: the starting disk track number
                arg4: the starting disk sector number
                arg5: the unit number of the disk from which to read
   Returns - arg1: 0 if transfer was successful; the disk status register otherwise.
             arg4: -1 if illegal values are given as input; 0 otherwise.
   Side Effects - call diskReadReal
   ----------------------------------------------------------------------- */
void
diskRead(USLOSS_Sysargs* args)
{
    // check kernel mode
}/* diskRead */

/* ------------------------------------------------------------------------
   Name - diskReadReal
   Purpose - Reads sectors sectors from the disk indicated by unit, starting
             at track track and sector first.
   Parameters - Required by diskRead
   Returns - -1 if invalid parameters
              0 if sectors were read successfully
             >0 if disk’s status register
   Side Effects - none.
   ----------------------------------------------------------------------- */
int
diskReadReal(int unit, int track, int first, int sectors, void *buffer)
{
    // check kernel mode
    isKernelMode("diskReadReal");


    return 0;
} /* diskReadReal */

/* ------------------------------------------------------------------------
   Name - diskWrite
   Purpose - Writes one or more sectors to the disk.
   Parameters - arg1: the memory address from which to transfer.
                arg2: number of sectors to write.
                arg3: the starting disk track number.
                arg4: the starting disk sector number.
                arg5: the unit number of the disk to write.
   Returns - arg1: 0 if transfer was successful; the disk status register otherwise.
             arg4: -1 if illegal values are given as input; 0 otherwise.
   Side Effects - call diskWriteReal
   ----------------------------------------------------------------------- */
void
diskWrite(USLOSS_Sysargs* args)
{
    // check kernel mode
    isKernelMode("diskWrite");

} /* diskWrite */

/* ------------------------------------------------------------------------
   Name - diskWriteReal
   Purpose - Writes sectors sectors to the disk indicated by unit, starting
             at track track and sector first
   Parameters - Required by diskWrite
   Returns - -1 if invalid parameters
              0 if sectors were written successfully
             >0 if disk’s status register
   Side Effects - none.
   ----------------------------------------------------------------------- */
int
diskWriteReal(int unit, int track, int first, int sectors, void *buffer)
{
    // check kernel mode
    isKernelMode("diskWriteReal");

    return 0;
} /* diskWriteReal */

/* ------------------------------------------------------------------------
   Name - diskSize
   Purpose - Returns information about the size of the disk.
   Parameters - the unit number of the disk
   Returns - arg1: size of a sector, in bytes
             arg2: number of sectors in a track
             arg3: number of tracks in the disk
             arg4: -1 if illegal values are given as input; 0 otherwise.
   Side Effects - call diskSizeReal
   ----------------------------------------------------------------------- */
void
diskSize(USLOSS_Sysargs* args)
{
    // check kernel mode
    isKernelMode("diskSize");

} /* diskSize */

/* ------------------------------------------------------------------------
   Name - diskSizeReal
   Purpose - Returns information about the size of the disk indicated by unit.
   Parameters - Required by diskSize
   Returns - -1 if invalid parameters
              0 if disk size parameters returned successfully
   Side Effects - none.
   ----------------------------------------------------------------------- */
int
diskSizeReal(int unit, int *sector, int *track, int *disk)
{
    // check kernel mode
    isKernelMode("diskSizeReal");

    return 0;
} /* diskSizeReal */

/* ------------------------------------------------------------------------
   Name - termRead
   Purpose - Read a line from a terminal
   Parameters - arg1: address of the user’s line buffer.
                arg2: maximum size of the buffer.
                arg3: the unit number of the terminal from which to read.
   Returns - arg2: number of characters read.
             arg4: -1 if illegal values are given as input; 0 otherwise.
   Side Effects - call termReadReal
   ----------------------------------------------------------------------- */
void
termRead(USLOSS_Sysargs* args)
{
    // check kernel mode
    isKernelMode("termRead");

} /* termRead */

/* ------------------------------------------------------------------------
   Name - termReadReal
   Purpose - Reads a line of text from the terminal indicated
             by unit into the buffer pointed to by buffer
   Parameters - Required by termRead
   Returns - -1 if invalid parameters
             >0 if number of characters read
   Side Effects - none.
   ----------------------------------------------------------------------- */
int
termReadReal(int unit, int size, char *buffer)
{
    return 0;
} /* termReadReal */

/* ------------------------------------------------------------------------
   Name - termWrite
   Purpose - Write a line to a terminal
   Parameters - arg1: address of the user’s line buffer.
                arg2: number of characters to write.
                arg3: the unit number of the terminal to which to write.
   Returns - arg2: number of characters written.
             arg4: -1 if illegal values are given as input; 0 otherwise.
   Side Effects - call termWriteReal
   ----------------------------------------------------------------------- */
void
termWrite(USLOSS_Sysargs* args)
{
    // check kernel mode
    
} /* termWrite */

/* ------------------------------------------------------------------------
   Name - termWriteReal
   Purpose - Writes size characters — a line of text pointed to by text to
             the terminal indicated by unit
   Parameters - Required by termWrite
   Returns - -1 if invalid parameters
             >0 if number of characters written
   Side Effects - none.
   ----------------------------------------------------------------------- */
int
termWriteReal(int unit, int size, char *text)
{
    return 0;
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

/* Initialize the given Queue */
void initDiskQueue(diskQueue* q) {
    q->head = NULL;
    q->tail = NULL;
    q->curr = NULL;
    q->size = 0;
}

/* Adds the proc pointer to the disk queue in sorted order */
void addDiskQ(diskQueue* q, procPtr p) {
    USLOSS_Console("addDiskQ: adding pid %d, track %d to queue\n", p->pid, p->diskTrack);

    // first add
    if (q->head == NULL) { 
        q->head = q->tail = p;
        q->head->nextDiskPtr = q->tail->nextDiskPtr = NULL;
        q->head->prevDiskPtr = q->tail->prevDiskPtr = NULL;
    }
    else {
        // find the right location to add
        procPtr prev = q->tail;
        procPtr next = q->head;
        while (next != NULL && next->diskTrack <= p->diskTrack) {
            prev = next;
            next = next->nextDiskPtr;
            if (next == q->head)
                break;
        }
        USLOSS_Console("addDiskQ: found place, prev = %d\n", prev->diskTrack);
        prev->nextDiskPtr = p;
        p->prevDiskPtr = prev;
        if (next == NULL)
            next = q->head;
        p->nextDiskPtr = next;
        next->prevDiskPtr = p;
        if (p->diskTrack < q->head->diskTrack)
            q->head = p; // update head
        if (p->diskTrack >= q->tail->diskTrack)
            q->tail = p; // update tail
    }
    q->size++;
    USLOSS_Console("addDiskQ: add complete, size = %d\n", q->size);
} 

/* queues a sleeper into the sleep queue based on its wake up time */
void enqueueSleeper(procPtr p) {
	// first add
	diskQueue* q = &sleepQueue;
	if (q->head == NULL) {
		q->head = q->tail = p;
		q->head->nextDiskPtr = q->tail->nextDiskPtr = NULL;
		q->head->prevDiskPtr = q->tail->prevDiskPtr = NULL;
	}
	else {
		// find the right location to add
		procPtr prev = q->tail;
		procPtr next = q->head;
		while (next != NULL && next->wakeTime <= p->wakeTime) {
			prev = next;
			next = next->nextDiskPtr;
			if (next == q->head)
				break;
		}
		prev->nextDiskPtr = p;
		p->prevDiskPtr = prev;
		if (next == NULL)
			next = q->head;
		p->nextDiskPtr = next;
		next->prevDiskPtr = p;
		if (p->diskTrack < q->head->diskTrack)
			q->head = p; // update head
		if (p->diskTrack >= q->tail->diskTrack)
			q->tail = p; // update tail
	}
	q->size++;
}

/* Returns the next proc on the disk queue */
procPtr peekDiskQ(diskQueue* q) {
    if (q->curr == NULL) {
        q->curr = q->head;
    }
    return q->curr;
}

/* Returns and removes the next proc on the disk queue */
procPtr removeDiskQ(diskQueue* q) {
    if (q->size == 0)
        return NULL;

    if (q->curr == NULL) {
        q->curr = q->head;
    }

    USLOSS_Console("removeDiskQ: called, size = %d, curr pid = %d, curr track = %d\n", q->size, q->curr->pid, q->curr->diskTrack);

    procPtr temp = q->curr;

    if (q->size == 1) { // remove only node
        q->head = q->tail = q->curr = NULL;
    }

    else if (q->curr == q->head) { // remove head
        q->head = q->head->nextDiskPtr;
        q->head->prevDiskPtr = q->tail;
        q->tail->nextDiskPtr = q->head;
        q->curr = q->head;
    }

    else if (q->curr == q->tail) { // remove tail
        q->tail = q->tail->prevDiskPtr;
        q->tail->nextDiskPtr = q->head;
        q->head->prevDiskPtr = q->tail;
        q->curr = q->head;
    }

    else { // remove other
        q->curr->prevDiskPtr->nextDiskPtr = q->curr->nextDiskPtr;
        q->curr->nextDiskPtr->prevDiskPtr = q->curr->prevDiskPtr;
        q->curr = q->curr->nextDiskPtr;
    }

    q->size--;

    USLOSS_Console("removeDiskQ: done, size = %d, curr pid = %d, curr track = %d\n", q->size, temp->pid, temp->diskTrack);

    return temp;
} 

































