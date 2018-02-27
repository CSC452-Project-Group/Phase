#ifndef _MESSAGE_H_
#define _MESSAGE_H_
#define DEBUG2 1

typedef struct mailSlot *slotPtr;
typedef struct mailbox   mailbox;
typedef struct mboxProc *mboxProcPtr;
typedef struct queue    queue;
typedef struct mailSlot  mailSlot;
typedef struct mboxProc mboxProc;

#define SLOTQUEUE 0
#define PROCQUEUE 1

struct mboxProc {
    mboxProcPtr     nextMboxProc;
    int             pid;     // process ID
    char            msg_ptr[MAX_MESSAGE]; // where to put received message
    int             msg_size;
    int             messageReceived; // mail slot containing message we've received
};

struct queue {
    void *head;
    void *tail;
    int  size;
    int  ID;
};

struct mailbox {
    int       mboxID;
    // other items as needed...
    int       status;
    int       totalSlots;
	int       curSlots;
    int       slotSize;
    queue     slotq;
    queue     bProcS;
    queue     bProcR;
};

struct mailSlot {
    int       mboxID;
    int       status;
    // other items as needed...
    int       slotID;
    slotPtr   nextSlotPtr;
    char      message[MAX_MESSAGE];
    int       messageLen;
};

struct psrBits {
    unsigned int curMode:1;
    unsigned int curIntEnable:1;
    unsigned int prevMode:1;
    unsigned int prevIntEnable:1;
    unsigned int unused:28;
};

union psrValues {
    struct psrBits bits;
    unsigned int integerPart;
};

// mail box Macro
#define INACTIVE 0
#define ACTIVE   1

//mail slot Macro
#define EMPTY    0
#define USED     1

//process Macro
#define FULL     11
#define NONE     12
#define WAIT     13

//for message recieved
#define FALSE    0
#define TRUE     1

#endif
