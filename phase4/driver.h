typedef struct procStruct procStruct;
typedef struct procStruct * procPtr;


/* 
* Process struct for phase 4
*/
struct procStruct {
  int             pid;
  int 		  mboxID; 
  int             blockSem;
  int		  wakeTime;
  int 		  diskTrack;
  int             unit;
  int             track;
  int             sectors;
  void*           buffer;
  //int 		  diskFirstSec;
  //int 		  diskSectors;
  //void 		  *diskBuffer;
  procPtr 	  prevDiskPtr;
  procPtr 	  nextDiskPtr;
  procPtr	  nextclockQueueProc;
  procPtr         nextdiskQueueProc;
  USLOSS_DeviceRequest diskRequest;
};
