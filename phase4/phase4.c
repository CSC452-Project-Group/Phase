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
procPtr deq4(diskQueue* q);

/* ---------------------------- Globals -------------------------------*/
int  semRunning;

int  ClockDriver(char *);
int  DiskDriver(char *);
int  TermDriver(char *);
int  TermReader(char *);
int  TermWriter(char *);
int diskWriteReal(int, int, int, int, void *);
void diskWrite(USLOSS_Sysargs*);
int diskReadOrWriteReal(int, int, int, int, void *, int);
int diskSizeReal(int, int*, int*, int*);
void diskSize(USLOSS_Sysargs*);
int termReadReal(int, int, char *);
void termRead(USLOSS_Sysargs*);
int termWriteReal(int, int, char *);
void termWrite(USLOSS_Sysargs*);
void push_clockQueue(procPtr);
void pop_clockQueue();
void isKernelMode(char *);
void setUserMode();
void initProc(int);
void initDiskQueue(diskQueue*);
void addDiskQ(diskQueue*, procPtr);
procPtr peekDiskQ(diskQueue*);
procPtr removeDiskQ(diskQueue*);
procPtr deq4(diskQueue*);
/* Globals */
procStruct ProcTable[MAXPROC];

int diskZapped; // indicates if the disk drivers are 'zapped' or not
diskQueue diskQs[USLOSS_DISK_UNITS]; // queues for disk drivers
diskQueue sleepQueue; // queue for the sleeping user proccesses
int diskPids[USLOSS_DISK_UNITS]; // pids of the disk drivers

typedef struct heap heap;
struct heap {
  int size;
  procPtr procs[MAXPROC];
};
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
//    char	name[128];
//    char        termbuf[10];
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
        //diskSizeReal(i, &temp, &temp, &ProcTable[pid % MAXPROC].diskTrack);
    }


    // May be other stuff to do here before going on to terminal drivers

    /*
     * Create terminal device drivers.
     */
    for (i = 0; i < USLOSS_TERM_UNITS; i++) {
        char termbuf[10];
        sprintf(termbuf, "%d", i); 
        termPID[i][0] = fork1("Term driver", TermDriver, termbuf, USLOSS_MIN_STACK, 2);
        termPID[i][1] = fork1("Term reader", TermReader, termbuf, USLOSS_MIN_STACK, 2);
        termPID[i][2] = fork1("Term writer", TermWriter, termbuf, USLOSS_MIN_STACK, 2);
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
    //pid = waitReal(&status);
    if ( waitReal(&status) != pid ) {
        USLOSS_Console("start3(): join returned something other than ");
        USLOSS_Console("start3's pid\n");
    }
    /*
     * Zap the device drivers
     */
    status = 0;
    zap(clockPID);
    join(&status);  // clock drive
    for (i = 0; i < USLOSS_DISK_UNITS; i++) {
	semvReal(ProcTable[diskPids[i]].blockSem);
	zap(diskPids[i]);
	join(&status);
    }

	//dumpProcesses();
    for (i = 0; i < USLOSS_TERM_UNITS; i++) {
        //semvReal(ProcTable[termPID[i][0]].blockSem);
        //zap(termPID[i][0]);
        //join(&status);        
        //semvReal(ProcTable[termPID[i][1]].blockSem);
        MboxSend(charRecvMbox[i], NULL, 0);
	zap(termPID[i][1]);        
        join(&status);         
    }
	//dumpProcesses();
    for (i = 0; i < USLOSS_TERM_UNITS; i++) {
	//semvReal(ProcTable[termPID[i][2]].blockSem);
        MboxSend(lineWriteMbox[i], NULL, 0);
	zap(termPID[i][2]);
        join(&status);         
     }
	//dumpProcesses();
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

		procPtr curTerm = &ProcTable[termPID[i][0]];
		semvReal(curTerm->blockSem);

        // file stuff
        sprintf(filename, "term%d.in", i);
        FILE *f = fopen(filename, "a+");
        fprintf(f, "last line\n");
        fflush(f);
        fclose(f);

        // actual termdriver zap
        zap(termPID[i][0]);
		USLOSS_Console("start3(): after zap of term %d \n", i);
        join(&ctrl);
		//dumpProcesses();
    }
	USLOSS_Console("Start3(): end of zaps\n");
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
		while (clockQueue != NULL && clockQueue->wakeTime < status) {
			//deq4(&sleepQueue);
			//USLOSS_Console("ClockDriver(): Waking up process %d\n", proc->pid);
			//USLOSS_Console("clockDriver(): semaphore %d\n", proc->blockSem);
			semvReal(clockQueue->blockSem);
			pop_clockQueue();
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
    int result;
    int status;
    int unit = atoi( (char *) arg);     // Unit is passed as arg.

    // set up the proc table
    initProc(getpid());
    procPtr me = &ProcTable[getpid() % MAXPROC];
    initDiskQueue(&diskQs[unit]);

    //USLOSS_Console("DiskDriver: unit %d started, pid = %d\n", unit, me->pid);

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
        //USLOSS_Console("DiskDriver: unit %d unblocked, zapped = %d, queue size = %d\n", unit, isZapped(), diskQs[unit].size);
        
        if (isZapped()){
            return 0;
	}
        // get request off queue
        if (diskQs[unit].size > 0) {
            procPtr proc = peekDiskQ(&diskQs[unit]);
            int track = proc->diskTrack;

           // USLOSS_Console("DiskDriver: taking request from pid %d, track %d\n", proc->pid, proc->diskTrack);
            
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
  
					//USLOSS_Console("DiskDriver: seeked to track %d, status = %d, result = %d\n", track, status, result);

                    
                    //USLOSS_Console("DiskDriver: seeked to track %d, status = %d, result = %d\n", track, status, result);

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

                        //USLOSS_Console("DiskDriver: read/wrote sector %d, status = %d, result = %d, buffer = %s\n", sec, status, result, proc->diskRequest.reg2);
                        

						proc->diskSectors--;
						proc->diskRequest.reg2 += USLOSS_DISK_SECTOR_SIZE;
					}

					// request first sector of next track
					track++;
					proc->diskFirstSec = 0;
				}
			}

            //USLOSS_Console("DiskDriver: finished request from pid %d\n", proc->pid, result, status);

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
    int result;
    int status;
    int unit = atoi( (char *) arg);     // Unit is passed as arg.

    semvReal(semRunning);
    //USLOSS_Console("TermDriver (unit %d): running\n", unit);

	procPtr me = &ProcTable[getpid() % MAXPROC];
	sempReal(me->blockSem);

    while (!isZapped()) {
		//USLOSS_Console("TermDriver//(): term driver with pid %d in while\n", getpid());
        result = waitDevice(USLOSS_TERM_INT, unit, &status);
        if (result != 0) {
			USLOSS_Console("TermDriver(): after waitDevice\n");
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
        if (ch == '\n' || next == MAXLINE) {
            //USLOSS_Console("TermReader (unit %d): line send\n", unit);

            line[next] = '\0'; // end with null
            MboxSend(lineReadMbox[unit], line, next);

            // reset line
            for (i = 0; i < MAXLINE; i++) {
                line[i] = '\0';
            } 
            next = 0;
        }
    }
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
    int unit = atoi( (char *) arg);     // Unit is passed as arg.
    int size;
    int ctrl = 0;
    int next;
    int status;
    char line[MAXLINE];

    semvReal(semRunning);
    //USLOSS_Console("TermWriter (unit %d): running\n", unit);

    while (!isZapped()) {
        size = MboxReceive(lineWriteMbox[unit], line, MAXLINE); // get line and size

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
        int pid; 
        MboxReceive(pidMbox[unit], &pid, sizeof(int));
        semvReal(ProcTable[pid % MAXPROC].blockSem);
        
        
    }

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
    isKernelMode("diskRead");

    int sectors = (long) args->arg2;
    int track = (long) args->arg3;
    int first = (long) args->arg4;
    int unit = (long) args->arg5;

    int retval = diskReadReal(unit, track, first, sectors, args->arg1);

    if (retval == -1) {
        args->arg1 = (void *) ((long) retval);
        args->arg4 = (void *) ((long) -1);
    } else {
        args->arg1 = (void *) ((long) retval);
        args->arg4 = (void *) ((long) 0);
    }
    setUserMode();    
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

	procPtr proc = &ProcTable[getpid()&MAXPROC];

    return diskReadOrWriteReal(unit, track, first, sectors, buffer, 0);
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
    int sectors = (long) args->arg2;
    int track = (long) args->arg3;
    int first = (long) args->arg4;
    int unit = (long) args->arg5;

    int retval = diskWriteReal(unit, track, first, sectors, args->arg1);

    if (retval == -1) {
        args->arg1 = (void *) ((long) retval);
        args->arg4 = (void *) ((long) -1);
    } else {
        args->arg1 = (void *) ((long) retval);
        args->arg4 = (void *) ((long) 0);
    }
    setUserMode();
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

    return diskReadOrWriteReal(unit, track, first, sectors, buffer, 1);
} /* diskWriteReal */

int diskReadOrWriteReal(int unit, int track, int first, int sectors, void *buffer, int write) {
    //USLOSS_Console("diskReadOrWriteReal: called with unit: %d, track: %d, first: %d, sectors: %d, write: %d\n", unit, track, first, sectors, write);

    // check for illegal args
    //if (unit < 0 || unit > 1 || track < 0 || track > ProcTable[diskPids[unit]].diskTrack ||
    //    first < 0 || first > USLOSS_DISK_TRACK_SIZE || buffer == NULL){//  ||
        //(first + sectors)/USLOSS_DISK_TRACK_SIZE + track > ProcTable[diskPids[unit]].diskTrack) {
    //    USLOSS_Console("Check status\n");
//	return -1;
    //}


    if (sectors < 0 || USLOSS_DISK_TRACK_SIZE <= first || unit < 0 || unit > 1 || track < 0 || track >= USLOSS_DISK_TRACK_SIZE){
	return -1;
    }
    procPtr driver = &ProcTable[diskPids[unit]];

    // init/get the process
    if (ProcTable[getpid() % MAXPROC].pid == -1) {
        initProc(getpid());
    }
    procPtr proc = &ProcTable[getpid() % MAXPROC];

    if (write)
        proc->diskRequest.opr = USLOSS_DISK_WRITE;
    else
        proc->diskRequest.opr = USLOSS_DISK_READ;
    proc->diskRequest.reg2 = buffer;
    proc->diskTrack = track;
    proc->diskFirstSec = first;
    proc->diskSectors = sectors;
    proc->diskBuffer = buffer;

    addDiskQ(&diskQs[unit], proc); // add to disk queue 
    semvReal(driver->blockSem);  // wake up disk driver
    sempReal(proc->blockSem); // block

    int status;
    int result = USLOSS_DeviceInput(USLOSS_DISK_DEV, unit, &status);

    //USLOSS_Console("diskReadOrWriteReal: finished, status = %d, result = %d\n", status, result);

    return result;
}

/* extract values from sysargs and call diskSizeReal */
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
    // check for illegal args
    if (unit < 0 || unit > 1 || sector == NULL || track == NULL || disk == NULL) {
        //USLOSS_Console("diskSizeReal: given illegal argument(s), returning -1\n");
        return -1;
    }

    procPtr driver = &ProcTable[diskPids[unit]];

    // get the number of tracks for the first time
    if (driver->diskTrack == -1) {
        // init/get the process
        if (ProcTable[getpid() % MAXPROC].pid == -1) {
            initProc(getpid());
        }
        procPtr proc = &ProcTable[getpid() % MAXPROC];

        // set variables
        proc->diskTrack = 0;
        USLOSS_DeviceRequest request;
        request.opr = USLOSS_DISK_TRACKS;
        request.reg1 = &driver->diskTrack;
        proc->diskRequest = request;

        addDiskQ(&diskQs[unit], proc); // add to disk queue 
        semvReal(driver->blockSem);  // wake up disk driver
        sempReal(proc->blockSem); // block

        //USLOSS_Console("diskSizeReal: number of tracks on unit %d: %d\n", unit, driver->diskTrack);
    }

    *sector = USLOSS_DISK_SECTOR_SIZE;
    *track = USLOSS_DISK_TRACK_SIZE;
    *disk = driver->diskTrack;
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
    int retval = MboxReceive(lineReadMbox[unit], &line, MAXLINE);

    //USLOSS_Console("termReadReal (unit %d): size %d retval %d \n", unit, size, retval);

    if (retval > size) {
        retval = size;
    }
    memcpy(buffer, line, retval);

    return retval;
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

int getNumTracks(int unit, int* disk) {
	int status = -1;
	USLOSS_DeviceRequest request;
	request.opr = USLOSS_DISK_TRACKS;
	request.reg1 = (void*)(long)disk;

	MboxSend(semRunning, NULL, 0);
	int result = USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &request);
	result = waitDevice(USLOSS_DISK_DEV, unit, &status);
	MboxReceive(semRunning, NULL, 0);
	return result;
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
    //USLOSS_Console("addDiskQ: adding pid %d, track %d to queue\n", p->pid, p->diskTrack);

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
        //USLOSS_Console("addDiskQ: found place, prev = %d\n", prev->diskTrack);
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
    //USLOSS_Console("addDiskQ: add complete, size = %d\n", q->size);
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

    //USLOSS_Console("removeDiskQ: called, size = %d, curr pid = %d, curr track = %d\n", q->size, q->curr->pid, q->curr->diskTrack);

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

    //USLOSS_Console("removeDiskQ: done, size = %d, curr pid = %d, curr track = %d\n", q->size, temp->pid, temp->diskTrack);

    return temp;
} 

procPtr deq4(diskQueue* q) {
	procPtr temp = q->head;
	if (q->head == NULL) {
		return NULL;
	}
	if (q->head == q->tail) {
		q->head = q->tail = NULL;
	}
	else {
		q->head = q->head->nextDiskPtr;
	}
	q->size--;
	return temp;
}


void
push_clockQueue(procPtr node)
{
    if(clockQueue == NULL || clockQueue->wakeTime > node->wakeTime){
        procPtr curr = clockQueue;
        clockQueue = node;
        node->nextclockQueueProc = curr;
    }
    else{
        procPtr curr = clockQueue;
        while(curr->nextclockQueueProc != NULL && curr->nextclockQueueProc->wakeTime <= node->wakeTime)
            curr = curr->nextclockQueueProc;
        node->nextclockQueueProc = curr->nextclockQueueProc;
        curr->nextclockQueueProc = node;
    } 
}

void
pop_clockQueue()
{
    if(clockQueue != NULL)
        clockQueue = clockQueue->nextclockQueueProc;
}
