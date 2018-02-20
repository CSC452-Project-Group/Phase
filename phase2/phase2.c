/* ------------------------------------------------------------------------
   phase2.c

   University of Arizona
   Computer Science 452

   ------------------------------------------------------------------------ */

#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <message.h>
#include <handler.c>
/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
void disableInterrupts();
void enableInterrupts();
void isKernelMode(char *);
void InitialSlot(int);
void InitialBox(int);
void InitialQueue(queue*, int);
void enqueue(queue*, void*);
void *dequeue(queue*);
void *head(queue*);
void dumpSlots(int);
/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 0;

// the mail boxes 
mailbox MailBoxTable[MAXMBOX];

// also need array of mail slots, array of function ptrs to system call 
// handlers, ...

// mail slots
mailSlot MailSlotTable[MAXSLOTS];

// processes
mboxProc mboxProcTable[MAXPROC];

int boxNum, slotNum, curSlot;
int nextBoxID = 0, nextSlotID = 0, nextProc = 0;

//system call
void (*syscall_vec[MAXSYSCALLS])(USLOSS_Sysargs *args);



/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name - start1
   Purpose - Initializes mailboxes and interrupt vector.
             Start the phase2 test process.
   Parameters - one, default arg passed by fork1, not used here.
   Returns - one to indicate normal quit.
   Side Effects - lots since it initializes the phase2 data structures.
   ----------------------------------------------------------------------- */
int start1(char *arg)
{
    int kidPid;
    int status;

    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): at beginning\n");
    
    //Test for kernel mode
    isKernelMode("start1()");
    
    //disable interrupts
    disableInterrupts();

    // Initialize the mail box table, slots, & other data structures.
    for (int i = 0; i < MAXMBOX; i++){
	InitialBox(i);
    }

    for (int i = 0; i < MAXSLOTS; i++){
        InitialSlot(i);
    }

    boxNum = 0;
    slotNum = 0;
	curSlot = 0;
    
    // Initialize USLOSS_IntVec and system call handlers,
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler2;
    USLOSS_IntVec[USLOSS_DISK_INT] = diskHandler;
    USLOSS_IntVec[USLOSS_TERM_INT] = termHandler;
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscallHandler;
    
    // allocate mailboxes for interrupt handlers.  Etc... 
    Mbox[CLOCK] = MboxCreate(0, sizeof(int)); // clock
    Mbox[DISK] = MboxCreate(0, sizeof(int)); // disk 1
    Mbox[DISK+1] = MboxCreate(0, sizeof(int)); // disk 2
    Mbox[TERM] = MboxCreate(0, sizeof(int)); // terminal 1
    Mbox[TERM+1] = MboxCreate(0, sizeof(int)); // terminal 2
    Mbox[TERM+2]= MboxCreate(0, sizeof(int)); // terminal 3
    Mbox[TERM+3] = MboxCreate(0, sizeof(int)); // terminal 4
    
    for (int i = 0; i < MAXSYSCALLS; i++) {
        syscall_vec[i] = nullsys;
    }
    
    enableInterrupts();

    // Create a process for start2, then block on a join until start2 quits
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): fork'ing start2 process\n");
    kidPid = fork1("start2", start2, NULL, 4 * USLOSS_MIN_STACK, 1);
    if ( join(&status) != kidPid ) {
        USLOSS_Console("start2(): join returned something other than ");
        USLOSS_Console("start2's pid\n");
    }

    return 0;
} /* start1 */

// Initializes the mailbox. i: index of the box in the mailbox table.
void InitialBox(int i)
{
    MailBoxTable[i].mboxID = -1;
	MailBoxTable[i].curSlots = 0;
    MailBoxTable[i].status = INACTIVE;
    MailBoxTable[i].totalSlots = -1;
    MailBoxTable[i].slotSize = -1;
    boxNum--; 
}


// Initializes a mail slot. i: Index of the mail slot in the mail slot table.
void InitialSlot(int i)
{
    MailSlotTable[i].mboxID = -1;
    MailSlotTable[i].status = EMPTY;
    MailSlotTable[i].slotID = i;
	
}


/* ------------------------------------------------------------------------
   Name - MboxCreate
   Purpose - gets a free mailbox from the table of mailboxes and initializes it 
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns - -1 to indicate that no mailbox was created, or a value >= 0 as the
             mailbox id.
   Side Effects - initializes one element of the mail box array. 
   ----------------------------------------------------------------------- */
int MboxCreate(int slots, int slot_size)
{
    //Test for kernel mode
    isKernelMode("MboxCreate()");

    //disable interrupts
    disableInterrupts();
    
    //check the mail box avilable
    if(boxNum == MAXMBOX || slots < 0 || slot_size < 0 || slot_size > MAX_MESSAGE){
	if(DEBUG2 && debugflag2)
	    USLOSS_Console("MboxCreate(): illegal args or max boxes reached, returning -1\n");
	return -1;
    }

    //find the first available index
    if(nextBoxID >= MAXMBOX || MailBoxTable[nextBoxID].status == ACTIVE){
	for(int i=0; i< MAXMBOX; i++){
	    if(MailBoxTable[i].status == INACTIVE){
	        nextBoxID = i;
		break;
	    }
	}
    }

    mailbox *box = &MailBoxTable[nextBoxID];

    box->mboxID = nextBoxID++;
    box->totalSlots = slots;
    box->slotSize = slot_size;
    box->status = ACTIVE;

    InitialQueue(&box->slotq, SLOTQUEUE);
    InitialQueue(&box->bProcS, PROCQUEUE);
    InitialQueue(&box->bProcR, PROCQUEUE);

    boxNum++;

    if (DEBUG2 && debugflag2){
        USLOSS_Console("MboxCreate(): created mailbox with id = %d, totalSlots = %d, slot_size = %d, numBoxes = %d\n", box->mboxID, box->totalSlots, box->slotSize, boxNum);
    }

    enableInterrupts();
    return box->mboxID;
} /* MboxCreate */


/* ------------------------------------------------------------------------
   Name - MboxSend
   Purpose - Put a message into a slot for the indicated mailbox.
             Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size)
{
	disableInterrupts();
	isKernelMode("MboxSend");

	//USLOSS_Console("MboxSend: checking for errors\n");

	if (MailBoxTable[mbox_id].status == INACTIVE) {
		//USLOSS_Console("MboxSend(): mailbox %d is inactive\n", mbox_id);
		return -1;
	}

	
	if (msg_size > MailBoxTable[mbox_id].slotSize) {
		//USLOSS_Console("MboxSend(): message size %d is too large\n", msg_size);
		return -1;
	}
	

	if (MailBoxTable[mbox_id].curSlots == MailBoxTable[mbox_id].totalSlots) {
		//USLOSS_Console("MboxSend(): mailbox has no slots available\n");
		
		queue * waitq = &MailBoxTable[mbox_id].bProcR;
		mboxProc * proc = waitq->head;
		if (proc != NULL) {
			memcpy(proc->msg_ptr, msg_ptr, msg_size);
			proc->msg_size = msg_size;
			unblockProc(proc->pid);
			enableInterrupts();
			return 0;
		}

		mboxProc mproc;
        	mproc.nextMboxProc = NULL;
        	mproc.pid = getpid();
			memcpy(proc->msg_ptr, msg_ptr, msg_size);
        	mproc.msg_size = msg_size;
		enqueue(&(MailBoxTable[mbox_id]).bProcS, &mproc);
		blockMe(FULL);
		//USLOSS_Console("mBoxSend(): process %d starting again\n", getpid());
		disableInterrupts();
	}

	if (isZapped() || MailBoxTable[mbox_id].status == INACTIVE) {
		return -3;
	}

	if (slotNum == MAXSLOTS) {
		//USLOSS_Console("MboxSend(): No slots left\n");
		USLOSS_Halt(1);
	}

	//USLOSS_Console("MboxSend: after checking for errors\n");

	// Find an unused slot in the slot table
	while (MailSlotTable[curSlot%MAXSLOTS].status == USED) {
		curSlot++;
	}
	int slot = curSlot % MAXSLOTS;

	//USLOSS_Console("MboxSend(): adding to slot %d\n", slot);

	//USLOSS_Console("MboxSend: found a slot\n");

	MailSlotTable[slot].status = USED;
	//USLOSS_Console("MboxSend(): memcpy '%s'\n", msg_ptr);
	memcpy(MailSlotTable[slot].message, msg_ptr, msg_size);
	//USLOSS_Console("MboxSend(): memcpy'd '%s'\n", MailSlotTable[slot].message);
	MailSlotTable[slot].mboxID = mbox_id;
	MailSlotTable[slot].messageLen = msg_size;

	MailBoxTable[mbox_id].curSlots++;

	slotNum++;

	//USLOSS_Console("MboxSend: num slots %d\n", MailBoxTable[mbox_id].curSlots);

	enqueue(&MailBoxTable[mbox_id].slotq, &MailSlotTable[curSlot%MAXSLOTS]);

	//dumpSlots(mbox_id);

	// Unblock the first process in the list of processes waiting
	queue *temp = &MailBoxTable[mbox_id].bProcR;
	if (temp->size > 0) {
		mboxProc *proc = NULL;
		proc = dequeue(temp);

		//USLOSS_Console("MboxSend(): unblock proc %d", proc->pid);
		unblockProc(proc->pid);
		enableInterrupts();
	}

    return 0;
} /* MboxSend */


/* ------------------------------------------------------------------------
   Name - MboxReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
             Block the receiving process if no msg available.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxReceive(int mbox_id, void *msg_ptr, int msg_size)
{
	disableInterrupts();
	isKernelMode("MboxRecieve");

	// check for errors
	if (MailBoxTable[mbox_id].status == INACTIVE) {
		//USLOSS_Console("MboxRecieve(): mailbox %d is inactive\n", mbox_id);
		return -1;
	}

	// block proc if empty
	if (MailBoxTable[mbox_id].curSlots == 0) {
		//USLOSS_Console("MboxSend(): mailbox has no slots available\n");
		mboxProc mproc;
		mproc.nextMboxProc = NULL;
		mproc.pid = getpid();
		//mproc.msg_ptr = msg_ptr;
		mproc.msg_size = msg_size;
		enqueue(&(MailBoxTable[mbox_id]).bProcR, &mproc);
		blockMe(NONE);
		disableInterrupts();

		if (MailBoxTable[mbox_id].curSlots == 0) {
			if (mproc.msg_size > msg_size) {
				return -1;
			}
			memcpy(msg_ptr, mproc.msg_ptr, mproc.msg_size);
			dequeue(&(MailBoxTable[mbox_id]).bProcR);
			return mproc.msg_size;
		}
	}

	queue * slotq = NULL;
	slotq = &MailBoxTable[mbox_id].slotq;
	mailSlot * slot = slotq->head;
	if (slot != NULL && msg_size < slot->messageLen) {
		return -1;
	}

	if (isZapped() || MailBoxTable[mbox_id].status == INACTIVE) {
		return -3;
	}

	// copy message to the buffer
	mailSlot * temp = NULL;
	temp = dequeue(&(MailBoxTable[mbox_id].slotq));
	MailBoxTable[mbox_id].curSlots--;
	curSlot--;
	//USLOSS_Console("MboxRecieve(): message to memcpy '%s'\n", temp->message);
	memcpy(msg_ptr, temp->message, temp->messageLen);
	//USLOSS_Console("MboxRecieve(): message memcpy'd `%s'\n", msg_ptr);

	InitialSlot(temp->slotID);

	// Unblock the first process in the list of processes waiting
	queue *box = &MailBoxTable[mbox_id].bProcS;
	if (box->size > 0) {
		mboxProc *proc = NULL;
		proc = dequeue(box);

		//USLOSS_Console("MboxSend(): unblock proc %d", proc->pid);
		unblockProc(proc->pid);
		enableInterrupts();
	}

    return temp->messageLen;
} /* MboxReceive */

int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size)
{
	disableInterrupts();
	isKernelMode("MboxSend");

	//USLOSS_Console("MboxSend: checking for errors\n");

	if (MailBoxTable[mbox_id].status == INACTIVE) {
		//USLOSS_Console("MboxSend(): mailbox %d is inactive\n", mbox_id);
		return -1;
	}

	if (msg_size > MailBoxTable[mbox_id].slotSize) {
		//USLOSS_Console("MboxSend(): message size %d is too large\n", msg_size);
		return -1;
	}

	if (MailBoxTable[mbox_id].curSlots == MailBoxTable[mbox_id].totalSlots) {
		return -2;
	}

	if (isZapped() || MailBoxTable[mbox_id].status == INACTIVE) {
		return -3;
	}

	if (slotNum == MAXSLOTS) {
		return -2;
	}

	//USLOSS_Console("MboxSend: after checking for errors\n");

	// Find an unused slot in the slot table
	while (MailSlotTable[curSlot%MAXSLOTS].status == USED) {
		curSlot++;
	}
	int slot = curSlot % MAXSLOTS;

	//USLOSS_Console("MboxSend(): adding to slot %d\n", slot);

	//USLOSS_Console("MboxSend: found a slot\n");

	MailSlotTable[slot].status = USED;
	//USLOSS_Console("MboxSend(): memcpy '%s'\n", msg_ptr);
	memcpy(MailSlotTable[slot].message, msg_ptr, msg_size);
	//USLOSS_Console("MboxSend(): memcpy'd '%s'\n", MailSlotTable[slot].message);
	MailSlotTable[slot].mboxID = mbox_id;
	MailSlotTable[slot].messageLen = msg_size;

	MailBoxTable[mbox_id].curSlots++;

	slotNum++;

	//USLOSS_Console("MboxSend: num slots %d\n", MailBoxTable[mbox_id].curSlots);

	enqueue(&MailBoxTable[mbox_id].slotq, &MailSlotTable[curSlot%MAXSLOTS]);

	//dumpSlots(mbox_id);

	// Unblock the first process in the list of processes waiting
	queue *temp = &MailBoxTable[mbox_id].bProcR;
	if (temp->size > 0) {
		mboxProc *proc = NULL;
		proc = dequeue(temp);

		//USLOSS_Console("MboxSend(): unblock proc %d", proc->pid);
		unblockProc(proc->pid);
		enableInterrupts();
	}

	return 0;
}

int MboxCondReceive(int mbox_id, void *msg_ptr, int msg_size)
{
	disableInterrupts();
	isKernelMode("MboxRecieve");

	// check for errors
	if (MailBoxTable[mbox_id].status == INACTIVE) {
		//USLOSS_Console("MboxRecieve(): mailbox %d is inactive\n", mbox_id);
		return -1;
	}

	// block proc if empty
	if (MailBoxTable[mbox_id].curSlots == 0) {
		return -2;
	}

	if (isZapped() || MailBoxTable[mbox_id].status == INACTIVE) {
		return -3;
	}

	// copy message to the buffer
	mailSlot * temp = NULL;
	temp = dequeue(&(MailBoxTable[mbox_id].slotq));
	MailBoxTable[mbox_id].curSlots--;
	curSlot--;
	//USLOSS_Console("MboxRecieve(): message to memcpy '%s'\n", temp->message);
	memcpy(msg_ptr, temp->message, temp->messageLen);
	//USLOSS_Console("MboxRecieve(): message memcpy'd `%s'\n", msg_ptr);

	InitialSlot(temp->slotID);

	// Unblock the first process in the list of processes waiting
	queue *box = &MailBoxTable[mbox_id].bProcS;
	if (box->size > 0) {
		mboxProc *proc = NULL;
		proc = dequeue(box);

		//USLOSS_Console("MboxSend(): unblock proc %d", proc->pid);
		unblockProc(proc->pid);
		enableInterrupts();
	}

	return temp->messageLen;
}

int MboxRelease(int mailboxID)
{
    disableInterrupts();
    isKernelMode("MboxRelease()");

    mailbox *mb = &MailBoxTable[mailboxID];    

    //check if mailID is valid
    if(mailboxID < 0 || mailboxID >= MAXMBOX){
	if(DEBUG2 && debugflag2)
	    USLOSS_Console("MboxRelease(): called with invalid mailboxID: %d, returning -1\n", mailboxID);
	return -1;
    }

    //check if mailbox is in use
    if(mb == NULL || mb->status == INACTIVE){
        if(DEBUG2 && debugflag2)
            USLOSS_Console("MboxRelease(): mailbox %d is already released, returning -1\n", mailboxID);
        return -1;
    }

    //check if the process has been zapped
    if(isZapped() || mb->status == INACTIVE){
	if(DEBUG2 && debugflag2)
            USLOSS_Console("MboxRelease(): process was zapped, returning -3\n");
        return -3;
    }

    //remove all slots in the mailbox
    while(mb->slotq.size > 0){
	slotPtr slot = (slotPtr)dequeue(&mb->slotq);
        InitialSlot(slot->slotID);
    }

    //release the maibox
    InitialBox(mailboxID);
    if(DEBUG2 && debugflag2){
	USLOSS_Console("MboxRelease(): release mailbox %d\n", mailboxID);
    }

    //unblock processes blocked by receive
    while(mb->bProcR.size > 0){
	mboxProcPtr proc = (mboxProcPtr)dequeue(&mb->bProcR);
        unblockProc(proc->pid);
        disableInterrupts();
    }

    //unblock processes blocked by send
    while(mb->bProcS.size > 0){
        mboxProcPtr proc = (mboxProcPtr)dequeue(&mb->bProcS);
        unblockProc(proc->pid);
        disableInterrupts();
    }

    enableInterrupts();
    return 0;
}


/*
 * Disables the interrupts.
 */
void disableInterrupts()
{
    // turn the interrupts OFF iff we are in kernel mode
    // if not in kernel mode, print an error message and
    // halt USLOSS
    
    //Check kernel mode
    isKernelMode("disableInterrupts()");

    int status = USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_INT );
    if(status == USLOSS_ERR_INVALID_PSR){
        USLOSS_Console("disableInterrupts(): error invalid psr, (halting)");
        USLOSS_Halt(1);
    }


} /* disableInterrupts */

/*
 * Enables the interrupts.
 */
void enableInterrupts()
{
    // turn the interrupts ON if we are in kernel mode
    // if not in kernel mode, print an error message and
    // halt USLOSS
    isKernelMode("enableInterrupts()");

    int status = USLOSS_PsrSet( USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT );
    if(status == USLOSS_ERR_INVALID_PSR){
        USLOSS_Console("enableInterrupts(): error invalid psr, (halting)");
        USLOSS_Halt(1);
    }
}

/*
 * Checks if currently in kernel mode
 */
void isKernelMode(char *method) {
    if (!(USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet())) {
        USLOSS_Console("%s: called while in user mode, by process %d. Halting...\n", method, getpid());
		USLOSS_Halt(1);
    }
}


/*
-------------------------------------------------
 Help functions for data structure queue
-------------------------------------------------
*/

// Initialize the data structure
void InitialQueue(queue* q, int ID){
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    q->ID   = ID;
}

// Insert the pointer to the end of the exiting queue
void enqueue(queue* q, void* p){
    if (q->head == NULL && q->tail == NULL) {
        q->head = q->tail = p;
    } 
	else { /* TODO: handle different ID here, if it's for slot or process */
		if (q->ID == SLOTQUEUE) {
			((slotPtr)(q->tail))->nextSlotPtr = p;
			//q->tail = ((slotPtr)p)->nextSlotPtr;
		}

		else if (q->ID == PROCQUEUE) {
			((mboxProcPtr)(q->tail))->nextMboxProc = p;
			//q->tail = ((mboxProcPtr)p)->nextMboxProc;
		}
		q->tail = p;
	}
   
	
    q->size++;
}

// Remove and return the head of the queue
void* dequeue(queue* q){
    void* temp = q->head;
    if (q->head == NULL) {
        return NULL;
    }
    if (q->head == q->tail) {
        q->head = q->tail = NULL; 
    }
    else { /* TODO: handle different ID here, if it's for slot or process */
        if(q->ID == SLOTQUEUE)
	    q->head = ((slotPtr)(q->head))->nextSlotPtr;
	    //q->head = ((slotPtr)(q->head))->nextSlotPtr;

		if(q->ID == PROCQUEUE)
	    q->head = ((mboxProcPtr)(q->head))->nextMboxProc;
    }

    q->size--;
    return temp;
}

// Return the head of the queue
void* head(queue* q){
    return q->head;
}

int check_io(void)
{
    if (DEBUG2 && debugflag2)
	USLOSS_Console("check_io(): called\n");
    return 0;
}

void dumpSlots(int mbox_id) {

	USLOSS_Console("dumpSlot(): mailbox %d\n", mbox_id);
	queue * temp = NULL;
	temp = &(MailBoxTable[mbox_id].slotq);

	mailSlot * slot = temp->head;

	while (slot != NULL) {
		USLOSS_Console("----------------slot: %d, message: %s\n", slot->slotID, slot->message);
		slot = slot->nextSlotPtr;
	}
}
