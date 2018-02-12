
#define DEBUG2 1

typedef struct mailSlot *slotPtr;
typedef struct mailbox   mailbox;
typedef struct mboxProc *mboxProcPtr;
typedef struct queue    queue;

#define SLOTQUEUE 0
#define PROCQUEUE 1
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
    int       slotSize;
};

struct mailSlot {
    int       mboxID;
    int       status;
    // other items as needed...
    int       slotID;
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
