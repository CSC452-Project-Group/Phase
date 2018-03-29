/* Queue struct for processes */
typedef struct procStruct3 procStruct3;
typedef struct procStruct3 * procPtr3;
typedef struct procQueue procQueue;

#define BLOCKED 0
#define CHILDREN 1

struct procQueue {
	procPtr3 head;
	procPtr3 tail;
	int 	 size;
	int 	 type; /* which procPtr to use for next */
};

/* 
* Process struct for phase 3
*/
struct procStruct3 {
    int             pid;
    int 		    mboxID; /* 0 slot mailbox belonging to this process */
	int (* startFunc) (char *);   /* function where process begins */
    procPtr3     	nextProcPtr;
    procPtr3        nextSiblingPtr;
    procPtr3        parentPtr;
	procQueue 		childrenQueue;
};

/* 
* Semaphore struct
*/
typedef struct semaphore semaphore;
struct semaphore {
 	int 		id;
 	int 		value;
 	int 		startingValue;
 	procQueue   blockedProcs;
 	int 		priv_mBoxID;
 	int 		mutex_mBoxID;
 };

