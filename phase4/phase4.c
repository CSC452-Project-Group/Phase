#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <stdlib.h> /* needed for atoi() */
#include <driver.h>
#include <providedPrototypes.h>
/* ---------------------------- Prototypes -------------------------------*/
int sleepReal(int seconds);

/* ---------------------------- Globals -------------------------------*/
int  semRunning;

int  ClockDriver(char *);
int  DiskDriver(char *);
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
        sempReal(running); // wait for driver to start running

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

		//TODO: get head of sleep queue
		procPtr proc;
		while (sleepQueue != NULL && status >= sleepQueue->wakeTime) {
            	    // TODO: check every proccess in sleep queue to see if it should be awakened
		    proc = queueRemove(&sleepqueue);
                    USLOSS_Console("ClockDriver: Waking up process %d\n", proc->pid);
             	    semvReal(proc->blockSem); 
        	}
    }
    return 0;
    }
}

int Sleep(int seconds) {

	USLOSS_Sysargs sysArg;

	CHECKMODE;
	

}

int sleep(int seconds) {

	isKernelMode("sleep");
}

int sleepReal(int seconds) {

	isKernelMode("sleepReal");

	if (seconds < 0)
		return ERR_INVALID;

	return ERR_OK;
}

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
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

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
                USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &proc->diskRequest);
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
                    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &request);
                    result = waitDevice(USLOSS_DISK_DEV, unit, &status);
                    if (result != 0) {
                        return 0;
                    }

                    
                    USLOSS_Console("DiskDriver: seeked to track %d, status = %d, result = %d\n", track, status, result);

                    // read/write the sectors
                    int sec;
                    for (sec = proc->diskFirstSec; proc->diskSectors > 0 && sec < USLOSS_DISK_TRACK_SIZE; sec++) {
                        proc->diskRequest.reg1 = (void *) ((long) sec);
                        USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &proc->diskRequest);
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

    semvReal(running); // unblock parent
    return 0;
}

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
    USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE );
}

/* initializes proc struct */
void initProc(int pid) {
    requireKernelMode("initProc()"); 

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
    if (debug4)
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
        if (debug4)
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
    if (debug4)
        USLOSS_Console("addDiskQ: add complete, size = %d\n", q->size);
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

    if (debug4)
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

    if (debug4)
        USLOSS_Console("removeDiskQ: done, size = %d, curr pid = %d, curr track = %d\n", q->size, temp->pid, temp->diskTrack);

    return temp;
} 

































