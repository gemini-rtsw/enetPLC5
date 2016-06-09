#ifndef __drvOrnlPLC5_linux_h
#define __drvOrnlPLC5_linux_h 1

#include <time.h>
#include <sys/times.h>
#include "enetPlc5.h"
#include "ornl_sys_types.h"
#include "ornl_avl.h"
#include "ornl_thread.h"
#include "devManager.h"
#include "epicsMutex.h"

#define MAX_DEV_UNITS 32

typedef void *ornlPLC5;

typedef struct ofeConfigTag {
  int dataFormat;
  int minScale[4];
  int maxScale[4];
  int filter[4];
} ofeConfigType, *ofeConfigPtr;

typedef struct nivConfigTag {
  int dataFormat;
  int minScale[8];
  int maxScale[8];
  int filter[8];
} nivConfigType, *nivConfigPtr;

typedef struct nt1ConfigTag {
  int dataFormat;
  int tempScale;
  int minScale[8];
  int maxScale[8];
  int filter[8];
  int tcType[8];
} nt1ConfigType, *nt1ConfigPtr;

typedef struct novConfigTag {
  int dataFormat;
  int minScale[8];
  int maxScale[8];
  int minClamp[8];
  int maxClamp[8];
  int maxRampRate[8];
  int resetState[8];
  int resetValue[8];
} novConfigType, *novConfigPtr;

typedef struct nbv1ConfigTag {
  int dataFormat;
  int tempScale;
  int minScale[8];
  int maxScale[8];
  int minClamp[8];
  int maxClamp[8];
  int maxRampRate[8];
  int resetState[8];
  int resetValue[8];
  int filter[8];
  int tcType[8];
} nbv1ConfigType, *nbv1ConfigPtr;

typedef struct ifeConfigTag {
  int dataFormat;
  int inputType;
  int filter;
  int range[16];
  int minScale[16];
  int maxScale[16];
} ifeConfigType, *ifeConfigPtr;

typedef struct plc5DataBlockTag {
  epicsMutexId lock;
  int changed;                /* for "on-change" output threads */
  unsigned int numMsgXfered;
  unsigned int numWords;
  short *data;
  short *inputData; /* for output - this detects changes caused by
                       other agents */
  unsigned short *forceChange;
  int dataValid;
} plc5DataBlockType, *plc5DataBlockPtr;

typedef struct ornlPLC5RecTag {
  struct ornlPLC5RecTag *flink;
  THREAD_HANDLE t;
  enetPlc5Comm plc5;
  char devName[31+1];
  char ipAddr[31+1];
  unsigned int port;
  int unit;
  char plc5Addr[31+1];         /* if addr is N7:3 and numWords is 10, then */
  int plc5FileType;            /*  file num is 7                           */
  int plc5FileNum;             /*  file type is ornlPLC5_INT               */
  int plc5BaseOffset;          /*  base index is 3                         */
  int plc5MaxOffset;           /*  max index is 12                         */
  double scanRate;             /* seconds */
  unsigned int numWords;
  int operation;               /* ornlPLC5DevRead or ornlPLC5DevWrite */
  int initComplete;
  int transactionCompleted;
  plc5DataBlockType xferBlock;
} ornlPLC5RecType, *ornlPLC5RecPtr;

typedef void *ornlPLC5DevHandle;

typedef int (*readFunc)( ornlPLC5DevHandle, int * );
typedef int (*writeFunc)( ornlPLC5DevHandle, int );
typedef int (*forceFunc)( ornlPLC5DevHandle );

typedef struct ornlPLC5DevTag {
  plc5DataBlockPtr dataPtr;
  ornlPLC5RecPtr recPtr;				/* Points to plc5 record for direct types */
  char plc5Addr[31+1];					/* copied here to avoid parsing out each time */
  int moduleBase;                     /* data array index to module base */
  int offset;
  unsigned short setMask;
  unsigned short clrMask;
  int controlWordOffset;              /* ofe */
  unsigned short polaritySetMask;     /* ofe */
  unsigned short polarityClrMask;     /* ofe */
  readFunc readData;
  readFunc readOther;
  forceFunc checkForce;
  writeFunc writeData;
  ornlPLC5DevHandle check;	/* Optional check handle for Ao/Bo */
  unsigned short checkFlag;	/* Prevents PLC write during check process */
} ornlPLC5DevType, *ornlPLC5DevPtr;

/* error codes */
#define ornlPLC5_Success	1
#define ornlPLC5_Ignore		3
#define ornlPLC5_NotFound	2
#define ornlPLC5_NoMem		4
#define ornlPLC5_Missing	6
#define ornlPLC5_Failure	8
#define ornlPLC5_Timeout	10
#define ornlPLC5_InvSeqNum	12
#define ornlPLC5_BadType	14
#define ornlPLC5_BadParam	16
#define ornlPLC5_BadAddr	18
#define ornlPLC5_ListEmpty	20

/* file type codes */
#define ornlPLC5_INT		100
#define ornlPLC5_BIN		101
#define ornlPLC5_TIMER		102
#define ornlPLC5_STATUS		103
#define ornlPLC5_IO		104 /* DBM */

/* device opertion codes */
#define ornlPLC5DevWrite	1000
#define ornlPLC5DevRead		1001
#define ornlPLC5DevControl	1002
#define ornlPLC5DevDirect	1003

#define ornlPLC5ThreadExit	2000

#define ornlPLC5_read		3000
#define ornlPLC5_write		3001

void ornlPLC5ShowMsgs (
  int flag
);

int plc5Msgs ( void );

long ornlGetPlc5ListPtr (
  ornlPLC5RecPtr *ptr
);

void ornlPLC5DebugShow ( void );

void ornlPLC5DebugOn (
  int level
);

void ornlPLC5DebugOff ( void );

int ornlPLC5GetVal (
  ornlPLC5 ptr,
  int dataType,
  void *valuePtr
);

int ornlPLC5PutVal (
  ornlPLC5 ptr,
  int dataType,
  void *valuePtr
);

int ornlPLC5KillThread (
  ornlPLC5 ptr
);

void ornlPLC5ShowList ( void );

void ornlPLC5ShowCounters ( void );

void ornlPLC5ZeroCounters ( void );

void ornlPLC5ReadCounters (
  int *_numConnections,
  int *_numInitErrors,
  int *_numTimeouts,
  int *_numReadErrors,
  int *_numWriteErrors,
  int *_numMsgsSent,
  int *_numMsgsRcvd
);

void ornlPLC5IncTimeouts ( void );

void ornlPLC5IncMsgsSent ( void );

void ornlPLC5IncMsgsRcvd ( void );

void ornlPLC5ShowWriteBuffers ( void );

int configPLC5Modules (
  char *fileName
);

int ornlPLC5GetDevHandle (
  char *devName,
  int unit,
  char *moduleName,
  int channel,
  char *plc5Addr,
  ornlPLC5DevHandle *handle
);

int ornlPLC5ReadWord (
  ornlPLC5DevHandle handle,
  int *value
);

int ornlPLC5ReadOtherWord (
  ornlPLC5DevHandle handle,
  int *value
);

int ornlPLC5CheckForce (
  ornlPLC5DevHandle handle
);

int ornlPLC5ReadBit (
  ornlPLC5DevHandle handle,
  int *value
);

int ornlPLC5ReadOtherBit (
  ornlPLC5DevHandle handle,
  int *value
);

int ornlPLC5WriteWord (
  ornlPLC5DevHandle handle,
  int value
);

int ornlPLC5WriteBit (
  ornlPLC5DevHandle handle,
  int value
);


#endif
