/*

   4/21/03
   John Sinclair
   sinclairjw@ornl.gov

   This code provides EPICS driver support for Allen-Bradley PLC5
   ethernet processors.

   The device manager is utilized to find instances of plc5 data table region
   descriptions. Each instance is identified by an address, e.g. N7:100,
   and is associated with an I/O direction (in or out) and a scan strategy
   (scan rate for inputs; scan rate[not implemented] or on-change for outputs).

   As each instance is processed, a memory region is created to
   hold the data to be transfered and an entry is added to a list that
   contains, among other things, the region name (for which the address
   is used, e.g. N7:100) and the address.

   Access to each data transfer memory region is arbitrated by a lock,
   one per section. Access to the list is arbitrated by a different lock.

   Device access code (contained in devOrnlPLC5.c) calls various functions
   to read or write data. Locks are thus acquired and released
   only by code contained in this file. No other file should manipulated the
   locks or the protected data structures directly.

   Support for generic word and bit access is provide and support for the
   following I/O modules are provided in a more device specific context:

   1771 NOV
   1771 NT1
   1771 NBV1
   1771 NIV
   1771 OFE
   1771 IFE

   PLC data tables types supported: O, I, N, B, T, S

   (Some items in T & S tables may not be supported)

*/

/* #define DUMMY 1 */

#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include "ctype.h"
#include "math.h"
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "dbDefs.h"
#include "dbAccess.h"
#include "dbFldTypes.h"
#include "registryFunction.h"
#include "drvSup.h"      /* EPICS driver support library         */
#include "epicsExport.h"

#include "drvSup.h"      /* EPICS driver support library         */
#include "drvOrnlPLC5-linux.h"

#define REMQHI( queue, buf, flag )\
    sys_remqh( (void *) (queue), (void **) (buf), (int) (flag) )

#define INSQTI( buf, queue, flag )\
    sys_insqt( (void *) (buf), (void *) (queue), (int) (flag) )

/* globals */

static unsigned int g_numConnections;
static unsigned int g_numInitErrors;
static unsigned int g_numTimeouts;
static unsigned int g_numWriteErrors;
static unsigned int g_numReadErrors;

static unsigned int g_numMsgsSent;
static unsigned int g_numBytesSent;
static unsigned int g_numMsgsRcvd;
static unsigned int g_numBytesRcvd;

static THREAD_HANDLE g_thLock;
static ornlPLC5RecPtr g_plc5RecListHead = NULL;
static ornlPLC5RecPtr g_plc5RecListTail = NULL;
static int g_status = 0; /* 1 = OK */
static THREAD_HANDLE g_delay;

static int g_msgs = 0;

/* forward decl's */

static long ornlPLC5PkgInit ( void );

static long ornlPLC5Report (
        int level
        );

typedef struct {
    long int number;
    DRVSUPFUN report;
    DRVSUPFUN init;
} drvOrnlPLC5Type;

drvOrnlPLC5Type drvOrnlPLC5 = {
    2,			/* Number of entries      */
    ornlPLC5Report,	/* Report routine         */
    ornlPLC5PkgInit	/* Initialization routine */
};
epicsExportAddress(drvet,drvOrnlPLC5);

static int g_initOnce = 1;
static int g_debug = 0;


void ornlPLC5ShowMsgs (
        int flag
        ) {

    g_msgs = flag;

}

int plc5Msgs ( void ) {

    return g_msgs;

}

static void delay (
        int ms
        ) {

    double s = (double) ms / 1000.0;

    thread_delay( g_delay, s );

}

static unsigned short bcd(
        int value
        ) {

    int i, abs_val, v;
    unsigned short us_val;

    us_val = 0;
    abs_val = abs( value );

    for ( i=0; i<4; i++ ) {

        v = ( abs_val % 10 ) << ( i * 4 );
        us_val |= v;
        abs_val /= 10;

    }

    return us_val;

}

/* Module dependent code */

/* generic plc data table read */

static int ornlPLC5ReadWordGeneric (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

#if 0
    /* printf( "ornlPLC5ReadWordGeneric\n" ); */
    printf( "devPtr->offset = %-d\n", devPtr->offset );
    /* *value = 0; */
    /* return 1; */
#endif

    epicsMutexLock( devPtr->dataPtr->lock );

    if ( !devPtr->dataPtr->dataValid ) {
        /* We can't do the read because we haven't read the PLC or the */
        /* connection has failed */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    *value = (int) devPtr->dataPtr->data[devPtr->offset];

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

static int ornlPLC5ReadOtherWordGeneric (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    /* printf( "ornlPLC5ReadOtherWordGeneric\n" ); */
    /* printf( "devPtr->offset = %-d\n", devPtr->offset ); */
    /* *value = 0; */
    /* return 1; */

    epicsMutexLock( devPtr->dataPtr->lock );

    if ( !devPtr->dataPtr->dataValid ) {
        /* We can't do the read because we haven't read the PLC or the */
        /* connection has failed */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    *value = (int) devPtr->dataPtr->inputData[devPtr->offset];

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

static int ornlPLC5CheckForceGeneric (
        ornlPLC5DevHandle handle
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    int result;

    epicsMutexLock( devPtr->dataPtr->lock );

    if ( !devPtr->dataPtr->dataValid ) {
        /* We can't do the check because we haven't read the PLC or the */
        /* connection has failed */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return 0;
    }

    result = (int) ( devPtr->dataPtr->forceChange[devPtr->offset] != 0 );
    devPtr->dataPtr->forceChange[devPtr->offset] = 0;

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return result;

}

/* generic plc data table bit read */

static int ornlPLC5ReadWordDirect (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    /*  static ornlPLC5RecPtr rec = NULL; */
    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    int stat;
    short words[8];

    /*  if (rec == NULL) {
        rec = (ornlPLC5RecPtr) handle;
        printf( "ornlPLC5ReadWordDirect first time call rec->plc5 %p\n", rec->plc5 );
        return ornlPLC5_Success;
        }
        */
#if 0
    printf( "ornlPLC5ReadWordDirect devPtr->recPtr->plc5 %p addr %s\n", devPtr->recptr->plc5, devPtr->plc5Addr);
#endif


    epicsMutexLock( devPtr->recPtr->xferBlock.lock );

    stat = enetPlc5ReadPlc( devPtr->recPtr->plc5, devPtr->plc5Addr, 1, words, NULL );
    if ( !( stat & 1 ) ) {
        printf( "ornlPLC5ReadWordDirect  error %d from enetPlc5ReadPlc\n", stat);
        epicsMutexUnlock( devPtr->recPtr->xferBlock.lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->recPtr->xferBlock.lock );

    *value = (int) words[0];

#if 0
    printf( "raw 0x%x value %d\n", *value, *value);
#endif

    return ornlPLC5_Success;

}


/* generic plc data table bit read */

static int ornlPLC5ReadBitGeneric (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    unsigned short s;

    /* *value = 0; */
    /* return 1; */

    epicsMutexLock( devPtr->dataPtr->lock );

    if ( !devPtr->dataPtr->dataValid ) {
        /* We can't do the read because we haven't read the PLC or the */
        /* connection has failed */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    s = (unsigned short) devPtr->dataPtr->data[devPtr->offset] & devPtr->setMask;

    epicsMutexUnlock( devPtr->dataPtr->lock );

    *value = (int) ( s != 0 );

#if 0
    if (strstr(devPtr->plc5Addr,"O:066")) {
        printf( "ornlPLC5ReadBitGeneric %s\n", devPtr->plc5Addr );
        printf( "devPtr->offset = %-d\n", devPtr->offset );
        printf( "devPtr->setMask = 0x%04x\n", devPtr->setMask );
        printf( "devPtr->dataPtr->dataValid = %-d\n", devPtr->dataPtr->dataValid );
        printf( "devPtr->dataPtr->data[] = %-d\n", devPtr->dataPtr->data[devPtr->offset] );
        printf( "value = %-d\n", *value );
    }
#endif

    return ornlPLC5_Success;

}

/* generic plc data table bit read */

static int ornlPLC5ReadBitDirect (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    /*   static ornlPLC5RecPtr rec = NULL; */
    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    int stat, state;

    /*
       if (rec == NULL) {
       rec = (ornlPLC5RecPtr) handle;
       printf( "ornlPLC5ReadBitDirect first time call rec %p\n", rec );
       return ornlPLC5_Success;
       }
       */
    epicsMutexLock( devPtr->recPtr->xferBlock.lock );

#if 0
    printf( "ornlPLC5ReadBitDirect devPtr->recPtr->plc5 %p addr %s ", devPtr->recPtr->plc5, devPtr->plc5Addr);
#endif

    stat = enetPlc5ReadPlcBit( devPtr->recPtr->plc5, devPtr->plc5Addr, &state, NULL );
    if ( !( stat & 1 ) ) {
        printf( "ornlPLC5ReadBitDirect  error %d from enetPlc5ReadPlcBit\n", stat);
        epicsMutexUnlock( devPtr->recPtr->xferBlock.lock );
        return ornlPLC5_Failure;
    }

#if 0
    printf( "state %d\n", state);
#endif


    epicsMutexUnlock( devPtr->recPtr->xferBlock.lock );

    *value = ( state != 0 );

    return ornlPLC5_Success;

}

/* generic plc data table bit read */

static int ornlPLC5ReadOtherBitGeneric (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    unsigned short s;

    /* printf( "ornlPLC5ReadOtherBitGeneric\n" ); */
    /* printf( "devPtr->offset = %-d\n", devPtr->offset ); */
    /* printf( "devPtr->setMask = %-x\n", devPtr->setMask ); */
    /* printf( "devPtr->dataPtr->dataValid = %-d\n", devPtr->dataPtr->dataValid ); */
    /* *value = 0; */
    /* return 1; */

    epicsMutexLock( devPtr->dataPtr->lock );

    if ( !devPtr->dataPtr->dataValid ) {
        /* We can't do the read because we haven't read the PLC or the */
        /* connection has failed */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    s = (unsigned short) devPtr->dataPtr->inputData[devPtr->offset] &
        devPtr->setMask;

    epicsMutexUnlock( devPtr->dataPtr->lock );

    *value = (int) ( s != 0 );

    return ornlPLC5_Success;

}

static int ornlPLC5CheckForceBitGeneric (
        ornlPLC5DevHandle handle
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    unsigned short s;
    int result;

    epicsMutexLock( devPtr->dataPtr->lock );

    if ( !devPtr->dataPtr->dataValid ) {
        /* We can't do the check because we haven't read the PLC or the */
        /* connection has failed */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return 0;
    }

    s = devPtr->dataPtr->forceChange[devPtr->offset];

    result = (int) ( ( s & devPtr->setMask ) != 0 );

    s &= devPtr->clrMask;
    devPtr->dataPtr->forceChange[devPtr->offset] = s;

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return result;

}

/* Read 8 input nt1 channel */

static int ornlPLC5Nt1ReadWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    unsigned short status;

    /* printf( "ornlPLC5Nt1ReadWord\n" ); */

    epicsMutexLock( devPtr->dataPtr->lock );

    if ( !devPtr->dataPtr->dataValid ) {
        /* We can't do the read because we haven't read the PLC or the */
        /* connection has failed */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    *value = (int) devPtr->dataPtr->data[devPtr->offset];

    status = (unsigned short) devPtr->dataPtr->data[devPtr->offset-1];
    if ( status & 0x303 ) {
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}
/* Read 6 input, 2 output nbv1 channel */

static int ornlPLC5Nbv1InReadWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    unsigned short status;

    /* printf( "ornlPLC5Nbv1InReadWord\n" ); */

    epicsMutexLock( devPtr->dataPtr->lock );

    if ( !devPtr->dataPtr->dataValid ) {
        /* We can't do the read because we haven't read the PLC or the */
        /* connection has failed */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    *value = (int) devPtr->dataPtr->data[devPtr->offset];

    status = (unsigned short) devPtr->dataPtr->data[devPtr->offset-1];
    if ( status & 0x303 ) {
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

/* Read initial value for 6 input, 2 output nbv1 channel */

static int ornlPLC5Nbv1OutReadWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    /* printf( "ornlPLC5nbv1OutReadWord\n" ); */

    epicsMutexLock( devPtr->dataPtr->lock );

    *value = (int) devPtr->dataPtr->data[devPtr->offset];

    if ( !devPtr->dataPtr->dataValid ) {
        /* we don't know if the data ever got to the PLC */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

static int ornlPLC5Nbv1OutReadOtherWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    epicsMutexLock( devPtr->dataPtr->lock );

    *value = (int) devPtr->dataPtr->inputData[devPtr->offset];

    if ( !devPtr->dataPtr->dataValid ) {
        /* we don't know if the data ever got to the PLC */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

/* Write 6 input, 2 output nbv1 channel */

static int ornlPLC5Nbv1OutWriteWord (
        ornlPLC5DevHandle handle,
        int value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    /* printf( "ornlPLC5nbv1OutWriteWord\n" ); */

    epicsMutexLock( devPtr->dataPtr->lock );

    devPtr->dataPtr->data[devPtr->offset] = (short) value;
    devPtr->dataPtr->inputData[devPtr->offset] = (short) value;
    devPtr->dataPtr->changed = 1;

    if ( !devPtr->dataPtr->dataValid ) {
        /* well still attempt the write but we don't */
        /* know if the data ever got to the PLC */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

/* Read 8 input niv channel */

static int ornlPLC5NivReadWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    unsigned short status;

    /* printf( "ornlPLC5NivReadWord\n" ); */

    epicsMutexLock( devPtr->dataPtr->lock );

    if ( !devPtr->dataPtr->dataValid ) {
        /* We can't do the read because we haven't read the PLC or the */
        /* connection has failed */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    *value = (int) devPtr->dataPtr->data[devPtr->offset];

    status = (unsigned short) devPtr->dataPtr->data[devPtr->offset-1];
    if ( status & 0x303 ) {
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

/* Read ife channel */

static int ornlPLC5IfeBinReadWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    epicsMutexLock( devPtr->dataPtr->lock );

    if ( !devPtr->dataPtr->dataValid ) {
        /* We can't do the read because we haven't read the PLC or the */
        /* connection has failed */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    *value = (int) devPtr->dataPtr->data[devPtr->offset];

    /* Note: underrange, overrange, and polarity bits are all selected
       with the polaritySetMask field */

    /* adjust polarity */
    if ( devPtr->dataPtr->data[devPtr->moduleBase+1] &
            devPtr->polaritySetMask ) {
        *value *= -1;
    }

    /* check out-of-range and invalid-scaling diagnostic bits */
    /* *** NO ***
       if ( devPtr->dataPtr->data[devPtr->moduleBase] & 6 ) {
       epicsMutexUnlock( devPtr->dataPtr->lock );
       return ornlPLC5_Failure;
       }
       */

    /* Check underrange/overrange bits for this channel */
    /* *** NO ***
       if ( ( devPtr->dataPtr->data[devPtr->moduleBase+1] &
       devPtr->polaritySetMask ) ||
       ( devPtr->dataPtr->data[devPtr->moduleBase+2] &
       devPtr->polaritySetMask ) ) {
       epicsMutexUnlock( devPtr->dataPtr->lock );
       return ornlPLC5_Failure;
       }
       */

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

/* generic plc data table write */

static int ornlPLC5WriteWordGeneric (
        ornlPLC5DevHandle handle,
        int value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    /* printf( "ornlPLC5WriteWordGeneric\n" ); */
    /* printf( "devPtr->offset = %-d\n", devPtr->offset ); */
    /* printf( "value = %-d\n", value ); */
    /* return 1; */

    epicsMutexLock( devPtr->dataPtr->lock );

    devPtr->dataPtr->data[devPtr->offset] = (short) value;
    devPtr->dataPtr->inputData[devPtr->offset] = (short) value;
    devPtr->dataPtr->changed = 1;

    if ( !devPtr->dataPtr->dataValid ) {
        /* well still attempt the write but we don't */
        /* know if the data ever got to the PLC */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}


/* Direct write mechanism for words */
static int ornlPLC5WriteWordDirect (
        ornlPLC5DevHandle handle,
        int value
        ) {

    /*  static ornlPLC5RecPtr rec = NULL; */
    int stat;
    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    short words[8];

    /*  if (rec == NULL) {
        rec = (ornlPLC5RecPtr) handle;
        printf( "ornlPLC5WriteWordDirect first time call rec %p\n", rec );
        return ornlPLC5_Success;
        }
        */
    words[0] = (short) value;

#if 0
    printf( "ornlPLC5WriteWordDirect mutex\n");
#endif

    epicsMutexLock( devPtr->recPtr->xferBlock.lock );

#if 0
    printf( "ornlPLC5WriteWordDirect devPtr->recPtr->plc5 %p addr %s value %d\n", devPtr->recPtr->plc5, devPtr->plc5Addr, value);
#endif

    stat = enetPlc5WritePlcWord( devPtr->recPtr->plc5, devPtr->plc5Addr, words, NULL );
    if ( !( stat & 1 ) ) {
        printf( "ornlPLC5WriteWordDirect  error %d from enetPlc5WritePlcWord\n", stat);
        epicsMutexUnlock( devPtr->recPtr->xferBlock.lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->recPtr->xferBlock.lock );

    return ornlPLC5_Success;

}

/* generic plc data table write */

static int ornlPLC5WriteBitGeneric (
        ornlPLC5DevHandle handle,
        int value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    unsigned short s;

    epicsMutexLock( devPtr->dataPtr->lock );

    s = (unsigned short) devPtr->dataPtr->data[devPtr->offset];
    if ( value ) {
        s |= devPtr->setMask;
    }
    else {
        s &= devPtr->clrMask;
    }
    devPtr->dataPtr->data[devPtr->offset] = s;
    devPtr->dataPtr->inputData[devPtr->offset] = s;
    devPtr->dataPtr->changed = 1;

    if ( !devPtr->dataPtr->dataValid ) {
        /* well still attempt the write but we don't */
        /* know if the data ever got to the PLC */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}


/* Direct write mechanism for bits */
static int ornlPLC5WriteBitDirect (
        ornlPLC5DevHandle handle,
        int value
        ) {

    /*   static ornlPLC5RecPtr rec = NULL; */
    int stat;
    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    /*  if (rec == NULL) {
        rec = (ornlPLC5RecPtr) handle;
        printf( "ornlPLC5WriteBitDirect first time call rec %p\n", rec );
        return ornlPLC5_Success;
        }
        */
    epicsMutexLock( devPtr->recPtr->xferBlock.lock );

#if 0
    printf( "ornlPLC5WriteBitDirect devPtr->recPtr->plc5 %p addr %s value %d\n", devPtr->recPtr->plc5, devPtr->plc5Addr, value);
#endif

    stat = enetPlc5WritePlcBit( devPtr->recPtr->plc5, devPtr->plc5Addr, value, NULL );
    if ( !( stat & 1 ) ) {
        printf( "ornlPLC5WriteBitDirect  error %d from enetPlc5WritePlcBit\n", stat);
        epicsMutexUnlock( devPtr->recPtr->xferBlock.lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->recPtr->xferBlock.lock );

#if 0
    printf( "ornlPLC5WriteBitDirect success\n");
#endif


    return ornlPLC5_Success;

}

/* read initial value for nov channel */

static int ornlPLC5NovReadWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    /* printf( "ornlPLC5NovReadWord\n" ); */

    epicsMutexLock( devPtr->dataPtr->lock );

    *value = (int) devPtr->dataPtr->data[devPtr->offset];

    if ( !devPtr->dataPtr->dataValid ) {
        /* we don't know if the data ever got to the PLC */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

static int ornlPLC5NovReadOtherWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    epicsMutexLock( devPtr->dataPtr->lock );

    *value = (int) devPtr->dataPtr->inputData[devPtr->offset];

    if ( !devPtr->dataPtr->dataValid ) {
        /* we don't know if the data ever got to the PLC */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

/* Write nov channel */

static int ornlPLC5NovWriteWord (
        ornlPLC5DevHandle handle,
        int value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    /* printf( "ornlPLC5novWriteWord\n" ); */

    epicsMutexLock( devPtr->dataPtr->lock );

    devPtr->dataPtr->data[devPtr->offset] = (short) value;
    devPtr->dataPtr->inputData[devPtr->offset] = (short) value;
    devPtr->dataPtr->changed = 1;

    if ( !devPtr->dataPtr->dataValid ) {
        /* well still attempt the write but we don't */
        /* know if the data ever got to the PLC */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

/* Read initial value for ofe channel */

static int ornlPLC5OfeBinReadWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    /* this assumes the ofe has been configured to used binary data */

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    /* printf( "ornlPLC5OfeBinReadWord\n" ); */

    epicsMutexLock( devPtr->dataPtr->lock );

    *value = (int) abs(devPtr->dataPtr->data[devPtr->offset]);

    if ( devPtr->dataPtr->data[devPtr->controlWordOffset] &
            devPtr->polaritySetMask ) {

        (*value) *= -1;

    }

    if ( !devPtr->dataPtr->dataValid ) {
        /* we don't know if the data ever got to the PLC */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

static int ornlPLC5OfeBinReadOtherWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    /* this assumes the ofe has been configured to used binary data */

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    epicsMutexLock( devPtr->dataPtr->lock );

    *value = (int) abs(devPtr->dataPtr->inputData[devPtr->offset]);

    if ( devPtr->dataPtr->inputData[devPtr->controlWordOffset] &
            devPtr->polaritySetMask ) {

        (*value) *= -1;

    }

    if ( !devPtr->dataPtr->dataValid ) {
        /* we don't know if the data ever got to the PLC */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

/* Write ofe channel */

static int ornlPLC5OfeBinWriteWord (
        ornlPLC5DevHandle handle,
        int value
        ) {

    /* this assumes the ofe has been configured to used binary data */

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;

    /* printf( "ornlPLC5OfeBinWriteWord\n" ); */

    epicsMutexLock( devPtr->dataPtr->lock );

    if ( value < 0 ) {
        devPtr->dataPtr->data[devPtr->controlWordOffset] |=
            devPtr->polaritySetMask;
        devPtr->dataPtr->inputData[devPtr->controlWordOffset] |=
            devPtr->polaritySetMask;
    }
    else {
        devPtr->dataPtr->data[devPtr->controlWordOffset] &=
            devPtr->polarityClrMask;
        devPtr->dataPtr->inputData[devPtr->controlWordOffset] &=
            devPtr->polarityClrMask;
    }

    devPtr->dataPtr->data[devPtr->offset] = (short) abs(value);
    devPtr->dataPtr->inputData[devPtr->offset] = (short) abs(value);
    devPtr->dataPtr->changed = 1;

    if ( !devPtr->dataPtr->dataValid ) {
        /* well still attempt the write but we don't */
        /* know if the data ever got to the PLC */
        epicsMutexUnlock( devPtr->dataPtr->lock );
        return ornlPLC5_Failure;
    }

    epicsMutexUnlock( devPtr->dataPtr->lock );

    return ornlPLC5_Success;

}

static int ornlPLC5NoRead (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    printf( "ornlPLC5NoRead\n" );

    return ornlPLC5_Failure;

}

static int ornlPLC5NoCheck (
        ornlPLC5DevHandle handle
        ) {

    /*  printf( "ornlPLC5NoCheck\n" ); */

    return 0;

}

static int ornlPLC5NoWrite (
        ornlPLC5DevHandle handle,
        int value
        ) {

    printf( "ornlPLC5NoWrite\n" );

    return ornlPLC5_Failure;

}
    
static void trimWhiteSpace (
        char *str )
{

    char buf[127+1];
    int first, last, i, ii, l;

    l = strlen(str);
    if ( l > 126 ) l = 126;

    ii = 0;

    i = 0;
    while ( ( i < l ) && isspace( str[i] ) ) {
        i++;
    }

    first = i;

    i = l-1;
    while ( ( i >= first ) && isspace( str[i] ) ) {
        i--;
    }

    last = i;

    for ( i=first; i<=last; i++ ) {
        buf[ii] = str[i];
        ii++;
    }

    buf[ii] = 0;

    strcpy( str, buf );

}

#define DONE -1

#define SIGN_OR_NUM 1
#define NUM 2

static int legalInt (
        char *str
        ) {

    char buf[127+1];
    int i, l, legal, state;

    strncpy( buf, str, 127 );
    trimWhiteSpace( buf );
    l = strlen(buf);
    if ( l < 1 ) return 0;

    state = SIGN_OR_NUM;
    i = 0;
    legal = 1;
    while ( state != DONE ) {

        if ( i >= l ) state = DONE;

        switch ( state ) {

            case SIGN_OR_NUM:

                if ( buf[i] == '-' ) {
                    i++;
                    state = NUM;
                    continue;
                }

                if ( buf[i] == '+' ) {
                    i++;
                    state = NUM;
                    continue;
                }

                if ( isdigit(buf[i]) ) {
                    i++;
                    state = NUM;
                    continue;
                }

                legal = 0;
                state = DONE;

                break;        

            case NUM:

                if ( isdigit(buf[i]) ) {
                    i++;
                    continue;
                }

                legal = 0;
                state = DONE;

                break;        

        }

    }

    return legal;

}

static int getFileInfo (
        char *string,
        int *fileType,
        int *fileNum
        ) {

    int l;

#if 0
    printf("getFileInfo %s\n", string);
#endif

    l = strlen(string);

    if ( l < 1 ) return ornlPLC5_BadAddr;

#if 0  
    if ( l == 1 ) {
        /* DBM */
        /*   *fileType = ornlPLC5_BIN; */
        *fileType = ornlPLC5_IO;
        if ( string[0] == 'O' )
            *fileNum = 0;
        else if ( string[0] == 'I' )
            *fileNum = 1;
        else {
            printf("getFileInfo IO %s\n", string);
            return ornlPLC5_BadAddr;
        }
        return ornlPLC5_Success;
    }
#endif

    switch ( string[0] ) {
        case 'O':
            *fileType = ornlPLC5_IO;
            *fileNum = 0;
            return ornlPLC5_Success;

        case 'I':
            *fileType = ornlPLC5_IO;
            *fileNum = 1;
            return ornlPLC5_Success;

        case 'N':
            *fileType = ornlPLC5_INT;
            break;

        case 'B':
            *fileType = ornlPLC5_BIN;
            break;

        case 'T':
            *fileType = ornlPLC5_TIMER;
            break;

        case 'S':
            *fileType = ornlPLC5_STATUS;
            break;

        default:
            printf("getFileInfo default %s\n", string);
            *fileType = 0;
            *fileNum = 0;
            return ornlPLC5_BadAddr;

    }

    *fileNum = atol( &string[1] );
    if ( *fileNum == 0 ) {
        printf("getFileInfo fileNum %s\n", string);
        return ornlPLC5_BadAddr;
    }
    return ornlPLC5_Success;

}

static int getOffset (
        char *string,
        int *offset
        ) {

    if ( !legalInt(string) ) return ornlPLC5_BadAddr;

    *offset = atol( string );

#if 0
    printf("Parsed decimal offset %s as %d\n", string, *offset);
#endif 

    return ornlPLC5_Success;

}

static int getOffsetOctal (
        char *string,
        unsigned int *offset
        ) {

    if ( !legalInt(string) ) return ornlPLC5_BadAddr;

    sscanf(string, "%o", offset);

#if 0
    printf("Parsed octal offset %s as %d\n", string, *offset);
#endif

    return ornlPLC5_Success;

}

static int getBit (
        char *string,
        int *bit
        ) {

    if ( !legalInt(string) ) return ornlPLC5_BadAddr;

    *bit = atol( string );

#if 0
    printf("Parsed decimal bit %s as %d\n", string, *bit);
#endif

    return ornlPLC5_Success;

}

static int getBitOctal (
        char *string,
        unsigned int *bit
        ) {

    if ( !legalInt(string) ) return ornlPLC5_BadAddr;

    sscanf(string, "%o", bit);

#if 0
    printf("Parsed octal bit %s as %d\n", string, *bit);
#endif

    return ornlPLC5_Success;

}

static int getTimerMember (
        char *string,
        unsigned int *bit
        ) {

    if (0 == strcmp(string, "EN")) {
        *bit = 0;
    } else if (0 == strcmp(string, "TT")) {
        *bit = 1;
    } else if (0 == strcmp(string, "DN")) {
        *bit = 2;
    } else if (0 == strcmp(string, "PRE")) {
        *bit = 3;
    } else if (0 == strcmp(string, "ACC")) {
        *bit = 4;
    } else {
        return ornlPLC5_BadAddr;
    }

#if 0
    printf("Parsed timer member %s as %d\n", string, *bit);
#endif

    return ornlPLC5_Success;

}

static int parsePLC5Address (
        int mode,
        char *addr,
        int *fileType,
        int *fileNum,
        int *offset,
        int *bit
        ) {

    /*
     * addresses will be in one of the two following forms
     *
     * <FILE>:<OFFSET>
     *
     * or
     *
     * <FILE>:<OFFSET>/<BIT>
     *
     * FILE will be either I, O, or a single character type and a decimal
     * number (e.g. B3, N7, T20 )
     */

#define DONE -1
#define GETTING_FILE_ID 1
#define GETTING_OFFSET 2
#define GETTING_BIT 3
#define GETTING_OPTIONAL_BIT 4

    char buf[255], *tk;
    int stat, state = GETTING_FILE_ID;

    strncpy( buf, addr, 255 );
    buf[254] = 0;

    tk = strtok( buf, ":/" );

    while ( state != DONE ) {

#if 0
        printf("State %d ", state);
#endif

        switch ( state ) {

            case GETTING_FILE_ID:

                if ( !tk ) {
                    printf("parsePLC5Address - GETTING_FILE_ID !tk addr = %s\n", addr);

                    return ornlPLC5_BadAddr;
                }
                /* get file type and file number */
                stat = getFileInfo( tk, fileType, fileNum );
                if ( !( stat & 1 ) ) {
                    printf("parsePLC5Address - GETTING_FILE_ID stat addr = %s\n", addr);

                    return stat;
                }

                state = GETTING_OFFSET;

                break;

            case GETTING_OFFSET:

                if ( !tk ) {
                    printf("parsePLC5Address - GETTING_OFFSET !tk addr = %s\n", addr);

                    return ornlPLC5_BadAddr;
                }

                if ((mode == 1) && (*fileType == ornlPLC5_IO)) {
                    stat = getOffsetOctal( tk, (unsigned int *) offset ); /* DBM */
                } else {
                    stat = getOffset( tk, offset );
                }
                if ( !( stat & 1 ) ) {
                    printf("parsePLC5Address - GETTING_OFFSET stat addr = %s\n", addr);
                    return stat;
                }


                if ( *fileType == ornlPLC5_BIN )
                    state = GETTING_OPTIONAL_BIT;
                else if ( *fileType == ornlPLC5_INT )
                    state = GETTING_OPTIONAL_BIT;
                else if ( *fileType == ornlPLC5_STATUS )
                    state = GETTING_OPTIONAL_BIT;
                else if ( *fileType == ornlPLC5_IO ) 
                    state = GETTING_OPTIONAL_BIT;
                else if ( *fileType == ornlPLC5_TIMER ) 
                    state = GETTING_OPTIONAL_BIT;
                else {
                    *bit = -1;
                    state = DONE;
                }

                break;

            case GETTING_BIT:

                if ( !tk ) {
                    printf("parsePLC5Address - GETTING_BIT !tk addr = %s\n", addr);

                    return ornlPLC5_BadAddr;
                }

                if ((mode == 1) && (*fileType == ornlPLC5_IO)) { /* DBM */
                    stat = getBitOctal( tk, (unsigned int *) bit );
                } else {
                    stat = getBit( tk, bit );
                }

                if ( !( stat & 1 ) ) {
                    printf("parsePLC5Address - GETTING_BIT stat addr = %s\n", addr);
                    return stat;
                }
                state = DONE;
                break;

            case GETTING_OPTIONAL_BIT:

                if ( !tk ) {
                    *bit = -1;
                } else {

                    if ((mode == 1) && (*fileType == ornlPLC5_IO)) { /* DBM */
                        stat = getBitOctal( tk, (unsigned int *) bit );
                    } else if (*fileType == ornlPLC5_TIMER) { /* DBM */
                        stat = getTimerMember( tk, (unsigned int *) bit );
                    } else {
                        stat = getBit( tk, bit );
                    }
                    if ( !( stat & 1 ) ) return stat;

                }

                state = DONE;
                break;

        }

        tk = strtok( NULL, ":/" );

    }

    return ornlPLC5_Success;

}

static int initWriteBuffer (
        ornlPLC5RecPtr rec
        ) {

    int retStat;

    retStat = ornlPLC5_Success;

#if 0
    printf( "\n\ninitWriteBuffer\n" );
    printf( "      devName = %s\n", rec->devName );
    printf( "       ipAddr = %s\n", rec->ipAddr );
    printf( "     plc5Addr = %s\n", rec->plc5Addr );
    printf( " plc5FileType = %d\n", rec->plc5FileType );
    printf( "  plc5FileNum = %d\n", rec->plc5FileNum );
    printf( "plc5BaseOffset = %d\n", rec->plc5BaseOffset );
    printf( " plc5MaxOffset = %d\n", rec->plc5MaxOffset );
    printf( "         port = %d\n", rec->port );
    printf( "         unit = %d\n", rec->unit );
    printf( "     numWords = %d\n", rec->numWords );
    printf( "    operation = %d\n", rec->operation );
    printf( "    scanrate = %-f\n\n", rec->scanRate );
#endif

    /* init xferBlock and allocate data buffer */

    rec->xferBlock.lock = epicsMutexCreate();

    rec->xferBlock.dataValid = 0;
    rec->xferBlock.changed = 0;
    rec->xferBlock.numMsgXfered = 0;
    rec->xferBlock.numWords = rec->numWords;
    rec->xferBlock.data = (short *) calloc( rec->numWords+8, sizeof(short) );
    if ( !rec->xferBlock.data ) {
        if ( plc5Msgs() ) printf( "initWriteBuffer - Memory allocation failed\n" );
        return ornlPLC5_NoMem;
    }

    /* allocate inputData buffer */
    rec->xferBlock.inputData = (short *) calloc( rec->numWords+8, sizeof(short) );
    if ( !rec->xferBlock.inputData ) {
        if ( plc5Msgs() ) printf( "initWriteBuffer - Memory allocation failed\n" );
        return ornlPLC5_NoMem;
    }

    /* allocate force change buffer */
    rec->xferBlock.forceChange = (unsigned short *) calloc( rec->numWords+8,
            sizeof(unsigned short) );
    if ( !rec->xferBlock.forceChange ) {
        if ( plc5Msgs() ) printf( "initWriteBuffer - Memory allocation failed\n" );
        return ornlPLC5_NoMem;
    }

    memset( rec->xferBlock.forceChange, 0xffff, rec->numWords );

    return ornlPLC5_Success;

}

static void *onChangeWriterThread (
        THREAD_HANDLE t
        ) {

#define NOT_CONNECTED 1
#define FIRST_CONNECTED 2
#define CONNECTED 3

    ornlPLC5RecPtr rec;
    int i, stat, conStat, needDelay, connected;
    int doReadCount = 0;
    int state = NOT_CONNECTED;

    rec = (ornlPLC5RecPtr) thread_get_app_data( t );

    thread_lock( g_thLock );

#if 0
    printf( "\n\nonChangeWriterThread\n" );
    printf( "      devName = %s\n", rec->devName );
    printf( "       ipAddr = %s\n", rec->ipAddr );
    printf( "     plc5Addr = %s\n", rec->plc5Addr );
    printf( " plc5FileType = %d\n", rec->plc5FileType );
    printf( "  plc5FileNum = %d\n", rec->plc5FileNum );
    printf( "plc5BaseOffset = %d\n", rec->plc5BaseOffset );
    printf( " plc5MaxOffset = %d\n", rec->plc5MaxOffset );
    printf( "         port = %d\n", rec->port );
    printf( "         unit = %d\n", rec->unit );
    printf( "     numWords = %d\n", rec->numWords );
    printf( "    operation = %d\n", rec->operation );
    printf( "    scanrate = %-f\n\n", rec->scanRate );
#endif

    rec->initComplete = 1;

    thread_unlock( g_thLock );

    epicsMutexLock( rec->xferBlock.lock );
    rec->xferBlock.dataValid = 0;
    rec->transactionCompleted = 1;
    epicsMutexUnlock(rec->xferBlock.lock);

    stat = enetPlc5Init( &rec->plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        return NULL;
    }

    while ( 1 ) {

        switch ( state ) {

            case NOT_CONNECTED:

                if ( plc5Msgs() ) printf( "onChangeWriterThread dev: [%s]   ipaddr: [%s]  plc5addr: [%s] state = NOT_CONNECTED\n",
                        rec->devName, rec->ipAddr, rec->plc5Addr );

                connected = 0;

                enetPlc5SetTimeout( rec->plc5, 10.0 );

                g_numConnections++;
                conStat = enetPlc5ConnectPlc( rec->plc5, rec->ipAddr );
                if ( !( conStat & 1 ) ) {
                    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", conStat );
                    delay( 1000 ); /* throttle reconnect attempt */
                }
                else {
                    state = FIRST_CONNECTED;
                }

                break;

            case FIRST_CONNECTED:

                needDelay = 0;

                if ( plc5Msgs() ) printf( "onChangeWriterThread dev: [%s]   ipaddr: [%s] plc5addr: [%s] state = FIRST_CONNECTED\n",
                        rec->devName, rec->ipAddr, rec->plc5Addr );

                epicsMutexLock(rec->xferBlock.lock);

                stat = enetPlc5ReadPlc( rec->plc5, rec->plc5Addr, rec->numWords,
                        rec->xferBlock.inputData, rec->xferBlock.lock );
                if ( !( stat & 1 ) ) {
                    needDelay = 1;
                    g_numWriteErrors++;
                    if ( ( stat == ENETPLC5_E_CON_RESET ) ||
                            ( stat == ENETPLC5_E_CON_CLOSED ) ||
                            ( stat == ENETPLC5_E_TIMEOUT ) ) {
                        state = NOT_CONNECTED;
                    }
                }

                if ( stat & 1 ) {

                    memcpy( rec->xferBlock.data, rec->xferBlock.inputData,
                            rec->numWords*sizeof(short) );

                    for ( i=0; i<rec->numWords; i++ ) {
                        rec->xferBlock.forceChange[i] = 0xffff;
                    }

                    state = CONNECTED;
                    rec->xferBlock.dataValid = 1;
                    stat = thread_init_timer( t, 0.1 );
                    doReadCount = 20;

                }
                else {

                    needDelay = 1;

                    if ( state != CONNECTED ) {
                        stat = enetPlc5DisconnectPlc( rec->plc5 );
                        if ( !( stat & 1 ) ) {
                            if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
                        }
                    }

                }

                epicsMutexUnlock(rec->xferBlock.lock);

                if ( needDelay ) delay( 1000 ); /* throttle retry attempt */

                break;

            case CONNECTED:

                epicsMutexLock( rec->xferBlock.lock );

                doReadCount--;
                if ( doReadCount <= 0 ) {

                    doReadCount = 20;

                    if ( !rec->xferBlock.changed ) {

                        stat = enetPlc5ReadPlc( rec->plc5, rec->plc5Addr, rec->numWords,
                                rec->xferBlock.inputData, rec->xferBlock.lock );

                        if ( !( stat & 1 ) ) {
                            g_numWriteErrors++;
                            if ( ( stat == ENETPLC5_E_CON_RESET ) ||
                                    ( stat == ENETPLC5_E_CON_CLOSED ) ||
                                    ( stat == ENETPLC5_E_TIMEOUT ) ) {
                                state = NOT_CONNECTED;
                            }
                        }

                    }

                }

                if ( ( state == CONNECTED ) && rec->xferBlock.changed ) {

                    if ( 1 ) printf( "onChangeWriterThread dev: [%s]   ipaddr: [%s]  plc5addr: [%s] numWords: [%d] state = CONNECTED\n",
                            rec->devName, rec->ipAddr, rec->plc5Addr, rec->numWords );


                    rec->xferBlock.changed = 0; /* reset */
                    stat = enetPlc5WritePlc( rec->plc5, rec->plc5Addr, rec->numWords,
                            rec->xferBlock.data, NULL ); /* no internal locking */

                    memcpy( rec->xferBlock.inputData, rec->xferBlock.data,
                            rec->numWords*sizeof(short) );

                    doReadCount = 20;

                    if ( !( stat & 1 ) ) {
                        g_numWriteErrors++;
                        if ( ( stat == ENETPLC5_E_CON_RESET ) ||
                                ( stat == ENETPLC5_E_CON_CLOSED ) ||
                                ( stat == ENETPLC5_E_TIMEOUT ) ) {
                            state = NOT_CONNECTED;
                        }
                    }

                }

                epicsMutexUnlock(rec->xferBlock.lock);

                if ( state != CONNECTED ) {
                    epicsMutexLock( rec->xferBlock.lock );
                    rec->xferBlock.dataValid = 0;
                    epicsMutexUnlock(rec->xferBlock.lock);
                    stat = enetPlc5DisconnectPlc( rec->plc5 );
                    if ( !( stat & 1 ) ) {
                        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
                    }
                    delay( 1000 ); /* throttle reconnect attempt */
                }

                printf( "onChangeWriterThread\n");

                stat = thread_wait_for_timer( t );

        }

    }

    return NULL;

}

static void *periodicReaderThread (
        THREAD_HANDLE t
        ) {

    ornlPLC5RecPtr rec;
    int stat, conStat, connected;

    rec = (ornlPLC5RecPtr) thread_get_app_data( t );

    thread_lock( g_thLock );

#if 0
    printf( "\n\nperiodicReaderThread\n" );
    printf( "      devName = %s\n", rec->devName );
    printf( "       ipAddr = %s\n", rec->ipAddr );
    printf( "     plc5Addr = %s\n", rec->plc5Addr );
    printf( " plc5FileType = %d\n", rec->plc5FileType );
    printf( "  plc5FileNum = %d\n", rec->plc5FileNum );
    printf( "plc5BaseOffset = %d\n", rec->plc5BaseOffset );
    printf( " plc5MaxOffset = %d\n", rec->plc5MaxOffset );
    printf( "         port = %d\n", rec->port );
    printf( "         unit = %d\n", rec->unit );
    printf( "     numWords = %d\n", rec->numWords );
    printf( "    operation = %d\n", rec->operation );
    printf( "    scanrate = %-f\n\n", rec->scanRate );
#endif

    /* init xferBlock and allocate data buffer */

    rec->xferBlock.lock = epicsMutexCreate();

    rec->xferBlock.changed = 0;
    rec->xferBlock.numMsgXfered = 0;
    rec->xferBlock.numWords = rec->numWords;
    rec->xferBlock.data = (short *) calloc( rec->numWords+8, sizeof(short) );
    if ( !rec->xferBlock.data ) {
        if ( plc5Msgs() ) printf( "periodicReaderThread - Memory allocation failed\n" );
        thread_unlock( g_thLock );
        return NULL;
    }

    rec->initComplete = 1;
    rec->xferBlock.dataValid = 0;

    thread_unlock( g_thLock );

    stat = enetPlc5Init( &rec->plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        return NULL;
    }

    connected = 0;
    while ( 1 ) {

        enetPlc5SetTimeout( rec->plc5, 10.0 );

        g_numConnections++;
        conStat = enetPlc5ConnectPlc( rec->plc5, rec->ipAddr );
        if ( !( conStat & 1 ) ) {
            if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", conStat );
        }
        else {
            connected = 1;
            /*      printf( "periodicReaderThread connected %s\n", rec->plc5Addr ); DBM */
        }


        stat = thread_init_timer( t, rec->scanRate );
        while ( connected ) {

            stat = enetPlc5ReadPlc( rec->plc5, rec->plc5Addr, rec->numWords,
                    rec->xferBlock.data, rec->xferBlock.lock );

#if 0 /* DBM */
            if (strstr(rec->plc5Addr, "T4")) { 
                printf("Read %s num %d stat %d\n", rec->plc5Addr, rec->numWords, stat);
            }
#endif


            if ( !( stat & 1 ) ) {
                g_numReadErrors++;
                if ( ( stat == ENETPLC5_E_CON_RESET ) ||
                        ( stat == ENETPLC5_E_CON_CLOSED ) ||
                        ( stat == ENETPLC5_E_TIMEOUT ) ) {
                    connected = 0;
                }
                epicsMutexLock(rec->xferBlock.lock);
                rec->xferBlock.dataValid = 0;
                epicsMutexUnlock(rec->xferBlock.lock);
            } else {
                thread_lock( g_thLock );
                rec->transactionCompleted = 1;
                thread_unlock( g_thLock );
                epicsMutexLock(rec->xferBlock.lock);
                rec->xferBlock.dataValid = 1;
                epicsMutexUnlock(rec->xferBlock.lock);
            }

            /*      printf( "periodicReaderThread\n"); */

            stat = thread_wait_for_timer( t );

        }

        if ( conStat & 1 ) {
            stat = enetPlc5DisconnectPlc( rec->plc5 );
            if ( !( stat & 1 ) ) {
                if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
            }
        }
        g_numConnections--;

        delay( 1000 ); /* throttle reconnect attempt */

    }

    /* execution never gets here */

    /* phony completion status - this plc becomes disabled */
    thread_lock( g_thLock );
    rec->initComplete = 1;
    rec->transactionCompleted = 1;
    thread_unlock( g_thLock );

    while ( 1 ) {

        printf( "\n\nperiodicReaderThread\n" );
        printf( "PLC connect failed and has been permanently aborted\n" );
        printf( "devName = %s\n", rec->devName );
        printf( " ipAddr = %s\n", rec->ipAddr );
        delay( 60000 );

    }

    /* execution never gets here */
    stat = enetPlc5Destroy( &rec->plc5 );
    if ( !( stat & 1 ) ) {
        printf( "Error %-d from enetPlc5Destroy\n", stat );
    }

    return NULL;

}

/**
  Makes connection to PLC for all direct access types
  */
static void *directThread (
        THREAD_HANDLE t
        ) {

#define NOT_CONNECTED 1
#define FIRST_CONNECTED 2
#define CONNECTED 3

    ornlPLC5RecPtr rec;
    int stat, conStat, needDelay, connected;
    int state = NOT_CONNECTED;

    rec = (ornlPLC5RecPtr) thread_get_app_data( t );

    thread_lock( g_thLock );

#if 0
    printf( "\n\ndirectThread\n" );
    printf( "      devName = %s\n", rec->devName );
    printf( "       ipAddr = %s\n", rec->ipAddr );
    printf( "     plc5Addr = %s\n", rec->plc5Addr );
    printf( " plc5FileType = %d\n", rec->plc5FileType );
    printf( "  plc5FileNum = %d\n", rec->plc5FileNum );
    printf( "plc5BaseOffset = %d\n", rec->plc5BaseOffset );
    printf( " plc5MaxOffset = %d\n", rec->plc5MaxOffset );
    printf( "         port = %d\n", rec->port );
    printf( "         unit = %d\n", rec->unit );
    printf( "     numWords = %d\n", rec->numWords );
    printf( "    operation = %d\n", rec->operation );
    printf( "    scanrate = %-f\n\n", rec->scanRate );
#endif

    rec->xferBlock.lock = epicsMutexCreate();
    rec->initComplete = 1;
    rec->transactionCompleted = 1;
    thread_unlock( g_thLock );

    stat = enetPlc5Init( &rec->plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        return NULL;
    }

#if 0
    printf( "directThread rec->plc5: [%p]\n", rec->plc5 );
#endif

    while ( 1 ) {

        switch ( state ) {

            case NOT_CONNECTED:

#if 0
                printf( "directThread dev: [%s]   ipaddr: [%s]  plc5addr: [%s] state = NOT_CONNECTED\n",
                        rec->devName, rec->ipAddr, rec->plc5Addr );
#endif

                connected = 0;

                enetPlc5SetTimeout( rec->plc5, 10.0 );

                g_numConnections++;
                conStat = enetPlc5ConnectPlc( rec->plc5, rec->ipAddr );
                if ( !( conStat & 1 ) ) {
                    printf( "Error %-d from enetPlc5ConnectPlc\n", conStat );
                    delay( 1000 ); /* throttle reconnect attempt */
                }
                else {
                    state = FIRST_CONNECTED;
                }

                break;

            case FIRST_CONNECTED:

                needDelay = 0;

#if 0
                printf( "directThread dev: [%s]   ipaddr: [%s] plc5addr: [%s] state = FIRST_CONNECTED\n",
                        rec->devName, rec->ipAddr, rec->plc5Addr );

                printf( "directThread dev: [%s]   ipaddr: [%s] state = FIRST_CONNECTED  rec %p\n",
                        rec->devName, rec->ipAddr, rec->plc5);
#endif

                /* initialize direct write and read functions for PLC handle */

                /*		  ornlPLC5ReadWordDirect((ornlPLC5DevHandle) rec, 0);
                                  ornlPLC5ReadBitDirect((ornlPLC5DevHandle) rec, 0);

                                  ornlPLC5WriteWordDirect((ornlPLC5DevHandle) rec, 0);
                                  ornlPLC5WriteBitDirect((ornlPLC5DevHandle) rec, 0);
                                  */	

                state = CONNECTED;

                break;

            case CONNECTED:

                return NULL;

        }

    }

    /* execution never gets here */
    stat = enetPlc5Destroy( &rec->plc5 );
    if ( !( stat & 1 ) ) {
        printf( "Error %-d from enetPlc5Destroy\n", stat );
    }

    return NULL;

}

void ornlPLC5DebugShow ( void ) {

    printf( "debug levels:\n" );
    printf( "  bit 0 : show receive msg data (hex)\n" );
    printf( "  bit 1 : show send msg data (hex)\n" );

    printf( "\nCurrent level = %-x (hex)\n", g_debug );

}


void ornlPLC5DebugOn (
        int level
        ) {

    /*
     * debug levels
     *   bit 0 : show receive msg data (hex)
     *   bit 1 : show send msg data (hex)
     */

    g_debug = level;

}

void ornlPLC5DebugOff ( void ) {

    g_debug = 0;

}

ornlPLC5RecPtr buildPlc5Record (
        char *name,
        propListPtr properies
        ) {

    ornlPLC5RecPtr rec;
    int bit, stat;

    rec = (ornlPLC5RecPtr) calloc( 1, sizeof(ornlPLC5RecType) );
    if ( !rec ) return NULL;

    strncpy( rec->devName, name, 31 );
    rec->devName[31] = 0;

    rec->unit = atol( properies->unit );

    strncpy( rec->ipAddr, properies->prop0.value, 31 );
    rec->ipAddr[31] = 0;

    rec->port = atol( properies->prop1.value );

    strncpy( rec->plc5Addr, properies->prop2.value, 31 );

    /*DBM */
#if 0
    printf("buildPlc5Record - %s type %s\n", rec->plc5Addr, properies->prop5.value );
#endif

    stat = parsePLC5Address(0, rec->plc5Addr, &rec->plc5FileType,
            &rec->plc5FileNum, &rec->plc5BaseOffset, &bit );

#if 0
    printf("plc5FileType %d plc5FileNum %d plc5BaseOffset %d bit %d\n", rec->plc5FileType,
            rec->plc5FileNum, rec->plc5BaseOffset, bit );
#endif

    rec->numWords = atol( properies->prop3.value );
    if ( rec->numWords < 1 ) rec->numWords = 1;

    rec->plc5MaxOffset = rec->plc5BaseOffset + rec->numWords - 1;

    if ( strcmp( properies->prop4.value, "on-change" ) == 0 ) {
        rec->scanRate = 0.0;
    }
    else {
        rec->scanRate = atof( properies->prop4.value );
        if ( rec->scanRate < 0.01 ) {
            rec->scanRate = 0.01;
        }
    }

    if ( strcmp( properies->prop5.value, "output" ) == 0 ) {
        rec->operation = ornlPLC5DevWrite;
    }  else if ( strcmp( properies->prop5.value, "direct" ) == 0 ) {
        rec->operation = ornlPLC5DevDirect;
    } else {
        rec->operation = ornlPLC5DevRead;
    }

    rec->initComplete = 0;   /* used to make sure output block has been */
    /* initialized by reading the PLC5 and also */
    /* that all threads have been created and are */
    /* executing */

    rec->transactionCompleted = 0;  /* Used only at startup to know when a */
    /* thread has connected to PLC and has */
    /* completed at least onetransaction */

#ifdef DUMMY
    printf( "\n" );
    printf( "--------------------------------------------------------------\n" );
    printf( "drvOrnlPLC5-linux - DUMMY configuration\n" );
    printf( "--------------------------------------------------------------\n" );
    printf( "\n" );
    rec->initComplete = 1;
    rec->transactionCompleted = 1;
#endif

    return rec;

}

/**
  Thread creation routine. The devices list has been parsed into a data structure, and is now
  processed to start the appropriate reader and writer threads for each data block.

  TRIUMF modification implements a direct access function, which avoids the
  onChangeWriterThread which writes the entire write block if a single bit changes in the
  block. Not suitable for scattered memory layout as in TR13.

  The direct access function opens a connection to the PLC and then uses it for all direct
  actions, reads and write of words and bits.
  */
static int ornlPLC5CreateThreads ( void ) {

    ornlPLC5RecPtr cur;
    int stat, more, count;
    char tName[31+1];

    cur = g_plc5RecListHead->flink;
    while ( cur ) {

        delay(50); /*DBM */

        thread_lock( g_thLock );

        /* printf( "dev name = [%s]\n", cur->devName ); */

        if ( cur->operation == ornlPLC5DevRead ) {

            /* printf( "Create read thread, scan rate = %-f\n", cur->scanRate ); */
            stat = thread_create_handle( &cur->t, (void *) cur );
            if ( !( stat & 1 ) ) {
                thread_unlock( g_thLock );
                return stat;
            }

            sprintf( tName, "%4.2fR-%s", cur->scanRate, cur->plc5Addr );

            /* stat = thread_set_name( cur->t, tName ); */

            stat = thread_set_proc_priority( cur->t, "L" );

            stat = thread_create_proc( cur->t, periodicReaderThread );
            if ( !( stat & 1 ) ) {
                thread_unlock( g_thLock );
                return stat;
            }

        } else  if ( cur->operation == ornlPLC5DevDirect ) {
#if 0
            printf( "Create thread for Direct type \n");
#endif
            stat = thread_create_handle( &cur->t, (void *) cur );
            if ( !( stat & 1 ) ) {
                thread_unlock( g_thLock );
                return stat;
            }

            sprintf( tName, "%4.2fR-%s", cur->scanRate, cur->plc5Addr );

            stat = thread_set_proc_priority( cur->t, "H" );

            stat = thread_create_proc( cur->t, directThread );
            if ( !( stat & 1 ) ) {
                thread_unlock( g_thLock );
                return stat;
            }

        } else { /* ornlPLC5DevWrite */

            if ( cur->scanRate > 0.0 ) {

                printf( "Scanning write thread not implemented, scan rate = %-f\n",
                        cur->scanRate );
                cur->initComplete = 1; /* dummy for now */
                cur->transactionCompleted = 1; /* dummy for now */

            } else {

                /* printf( "Create write thread, write values on-change\n" ); */

                /* Initialize the data transfer write buffer by reading the current
                   values in the PLC data table */
                stat = initWriteBuffer( cur );
                if ( !( stat & 1 ) ) {

                    printf(
                            "initWriteBuffer failed, writer thread will not be created\n" );

                    /* phony - thread never is created */
                    cur->initComplete = 1;
                    cur->transactionCompleted = 1;

                } else {

                    /* printf(
                       "write buf successfully initialized, create writer thread\n" ); */

                    stat = thread_create_handle( &cur->t, (void *) cur );
                    if ( !( stat & 1 ) ) {
                        thread_unlock( g_thLock );
                        return stat;
                    }

                    sprintf( tName, "W-%s", cur->plc5Addr );

                    /* stat = thread_set_name( cur->t, tName ); */

                    stat = thread_set_proc_priority( cur->t, "H" );
                    printf( "Write type thread priority  result 0x%x\n", stat);

                    stat = thread_create_proc( cur->t, onChangeWriterThread );
                    if ( !( stat & 1 ) ) {
                        thread_unlock( g_thLock );
                        return stat;
                    }
                }
            }
        }

        thread_unlock( g_thLock );
        cur = cur->flink;
    }


    /* Wait until all threads have completed initialization */

    do {

        delay( 1000 );

        more = 0;
        cur = g_plc5RecListHead->flink;
        while ( cur ) {

            thread_lock( g_thLock );
            if ( !cur->initComplete ) {
                more = 1;
            }
            thread_unlock( g_thLock );

            cur = cur->flink;

        }

    } while ( more );

    thread_lock( g_thLock );
    printf( "All EnetPLC5 threads have initialized\n" );
    thread_unlock( g_thLock );

    /* Wait until all threads have completed at least one transaction        */
    /* For writer threads, one transaction is simply a successful connection */
    /* to the PLC. For reader threads, it is a connection and a read.        */
    /* (This operation will timeout in 10 seconds)                           */

    count = 10;
    do {

        delay( 1000 );

        more = 0;
        cur = g_plc5RecListHead->flink;
        while ( cur ) {

            thread_lock( g_thLock );
            if ( !cur->transactionCompleted ) more = 1;
            thread_unlock( g_thLock );

            cur = cur->flink;

        }

        count--;

    } while ( more && count );

    thread_lock( g_thLock );
    printf( "All EnetPLC5 threads are ready\n" );
    thread_unlock( g_thLock );

    return ornlPLC5_Success;

}

static int doPkgInit ( void ) {

    int stat;
    propListPtr prop;
    ornlPLC5RecPtr cur;
    char *className, *name;

    g_status = 0; /* not OK */


    /* init and allocate */

    g_numConnections = 0;
    g_numReadErrors = 0;
    g_numWriteErrors = 0;
    g_numInitErrors = 0;
    g_numTimeouts = 0;
    g_numMsgsSent = 0;
    g_numBytesSent = 0;
    g_numMsgsRcvd = 0;
    g_numBytesRcvd = 0;

    stat = thread_init();
    if ( !( stat & 1 ) ) return stat;

    stat = thread_create_lock_handle( &g_thLock );
    if ( !( stat & 1 ) ) return stat;

    stat = thread_create_handle( &g_delay, NULL );
    if ( !( stat & 1 ) ) return stat;

    /* Create plc5 rec list sentinel nodes */
    g_plc5RecListHead = (ornlPLC5RecPtr) calloc( 1, sizeof(ornlPLC5RecType) );

    if ( !g_plc5RecListHead ) return ornlPLC5_NoMem;

    g_plc5RecListTail = g_plc5RecListHead;
    g_plc5RecListTail->flink = NULL;


    /* for each dev mgr record ... */
    stat = devMgrGetFirstFromAll( &className, &name, &prop );
    while ( prop ) {

        if ( strcmp( className, "plc5Enet" ) == 0 ) {

            cur = buildPlc5Record ( name, prop );

            if ( cur ) {
                g_plc5RecListTail->flink = cur;
                g_plc5RecListTail  = cur;
                g_plc5RecListTail->flink = NULL;
            }

        }

        stat = devMgrGetNextFromAll( &className, &name, &prop );

    }

    /* if list is not empty set status OK */
    if ( g_plc5RecListHead->flink ) {
        g_status = 1; /* OK */
    }
    else {
        return ornlPLC5_ListEmpty;
    }


    /* create one thread per record */
    stat = ornlPLC5CreateThreads();


    return ornlPLC5_Success;

}

static long ornlPLC5PkgInit ( void ) {

    int stat;

    if ( g_initOnce ) {
        stat = doPkgInit();
        if( !( stat & 1 ) ) {
            if ( stat == ornlPLC5_ListEmpty ) {
                printf( "ornlPLC5PkgInit - error from doPkgInit, list is empty\n" );
            }
            else {
                printf( "ornlPLC5PkgInit - error from doPkgInit, stat = %-d\n", stat );
            }
        }
        g_initOnce = 0;
    }

    return 0;

}

static long ornlPLC5Report (
        int level
        ) {

    printf( "ornlPLC5Report - not implemented\n" );

    return 0;

}

long ornlGetPlc5ListPtr (
        ornlPLC5RecPtr *ptr
        ) {

    *ptr = g_plc5RecListHead;
    return ornlPLC5_Success;

}

void ornlPLC5ShowWriteBuffers ( void ) {

    /* Show output buffer contents */

    ornlPLC5RecPtr cur;
    short data[110];
    unsigned int numWords, count, remain, c, i, dataIndex;
    char plc5Addr[31+1];

    cur = g_plc5RecListHead->flink;
    while ( cur ) {

        if ( cur->operation == ornlPLC5DevWrite ) {

            if ( cur->scanRate > 0.0 ) {

                thread_lock( g_thLock );
                printf( "Scanning write thread not implemented, scan rate = %-f\n",
                        cur->scanRate );
                thread_unlock( g_thLock );

            }
            else {

                epicsMutexLock(cur->xferBlock.lock);

                strncpy( plc5Addr, cur->plc5Addr, 31 );
                plc5Addr[31] = 0;

                numWords = cur->numWords;

                epicsMutexUnlock(cur->xferBlock.lock);


                count = numWords / 100;
                remain = numWords % 100;


                thread_lock( g_thLock );

                printf( "\n" );
                printf( "Write Buffer        IP: %s   PLC Addr: %s   Num Words: %-d\n",
                        cur->ipAddr, plc5Addr, numWords );
                printf( "\n" );

                thread_unlock( g_thLock );


                dataIndex = 0;
                for ( c=0; c<count; c++ ) {

                    epicsMutexLock(cur->xferBlock.lock);
                    for ( i=0; i<100; i++ ) {
                        data[i] = cur->xferBlock.data[dataIndex++];
                    }
                    epicsMutexUnlock(cur->xferBlock.lock);

                    thread_lock( g_thLock );

                    for ( i=0; i<100; i++ ) {
                        printf( "  %-d: %-d\n", i + c * 100, data[i] );
                    }

                    thread_unlock( g_thLock );

                }

                if ( remain ) {

                    epicsMutexLock(cur->xferBlock.lock);
                    for ( i=0; i<remain; i++ ) {
                        data[i] = cur->xferBlock.data[dataIndex++];
                    }
                    epicsMutexUnlock(cur->xferBlock.lock);

                    thread_lock( g_thLock );

                    for ( i=0; i<remain; i++ ) {
                        printf( "  %-d: %-d\n", i + count * 100, data[i] );
                    }

                    thread_unlock( g_thLock );

                }

                printf( "\n" );

            }

        }

        cur = cur->flink;

    }

}

void ornlPLC5ShowCounters ( void ) {

    thread_lock( g_thLock );
    printf( "\n" );
    printf( "g_numConnections = %-d\n", g_numConnections );
    printf( "g_numMsgsSent = %-d\n", g_numMsgsSent );
    printf( "g_numMsgsRcvd = %-d\n", g_numMsgsRcvd );
    printf( "g_numWriteErrors = %-d\n", g_numWriteErrors );
    printf( "g_numReadErrors = %-d\n", g_numReadErrors );
    printf( "g_numInitErrors = %-d\n", g_numInitErrors );
    printf( "g_numTimeouts = %-d\n", g_numTimeouts );
    printf( "\n" );
    thread_unlock( g_thLock );

}

void ornlPLC5ZeroCounters ( void ) {

    g_numReadErrors = 0;
    g_numWriteErrors = 0;
    g_numTimeouts = 0;
    g_numMsgsSent = 0;
    g_numBytesSent = 0;
    g_numMsgsRcvd = 0;
    g_numBytesRcvd = 0;

}

void ornlPLC5ReadCounters (
        int *_numConnections,
        int *_numInitErrors,
        int *_numTimeouts,
        int *_numReadErrors,
        int *_numWriteErrors,
        int *_numMsgsSent,
        int *_numMsgsRcvd
        ) {

    *_numConnections = g_numConnections;
    *_numInitErrors = g_numInitErrors;
    *_numTimeouts = g_numTimeouts;
    *_numMsgsSent = g_numMsgsSent;
    *_numMsgsRcvd = g_numMsgsRcvd;
    *_numWriteErrors = g_numWriteErrors;
    *_numReadErrors = g_numReadErrors;

}

void ornlPLC5IncTimeouts ( void ) {

    g_numTimeouts++;

}

void ornlPLC5IncMsgsSent ( void ) {

    g_numMsgsSent++;

}

void ornlPLC5IncMsgsRcvd ( void ) {

    g_numMsgsRcvd++;

}


int ornlPLC5GetDevHandle (
        char *devName,
        int unit,
        char *moduleName,
        int channel,
        char *plc5Addr,
        ornlPLC5DevHandle *handle
        ) {

    ornlPLC5DevPtr devPtr;
    ornlPLC5RecPtr cur;
    int stat, fileType, fileNum, offset, bit;

    devPtr = (ornlPLC5DevPtr) calloc( 1, sizeof(ornlPLC5DevType) );

    if ( strcmp( moduleName, "ofeBin" ) == 0 ) {
        devPtr->writeData = ornlPLC5OfeBinWriteWord;
        devPtr->readData = ornlPLC5OfeBinReadWord;
        devPtr->readOther = ornlPLC5OfeBinReadOtherWord;
        devPtr->checkForce = ornlPLC5CheckForceGeneric;
    }
    else if ( strcmp( moduleName, "ifeBin" ) == 0 ) {
        devPtr->writeData = ornlPLC5NoWrite;
        devPtr->readData = ornlPLC5IfeBinReadWord;
        devPtr->readOther = ornlPLC5NoRead;
        devPtr->checkForce = ornlPLC5NoCheck;
    }
    else if ( strcmp( moduleName, "nov" ) == 0 ) {
        devPtr->writeData = ornlPLC5NovWriteWord;
        devPtr->readData = ornlPLC5NovReadWord;
        devPtr->readOther = ornlPLC5NovReadOtherWord;
        devPtr->checkForce = ornlPLC5CheckForceGeneric;
    }
    else if ( strcmp( moduleName, "niv" ) == 0 ) {
        devPtr->writeData = ornlPLC5NoWrite;
        devPtr->readData = ornlPLC5NivReadWord;
        devPtr->readOther = ornlPLC5NoRead;
        devPtr->checkForce = ornlPLC5NoCheck;
    }
    else if ( strcmp( moduleName, "nt1" ) == 0 ) {
        devPtr->writeData = ornlPLC5NoWrite;
        devPtr->readData = ornlPLC5Nt1ReadWord;
        devPtr->readOther = ornlPLC5NoRead;
        devPtr->checkForce = ornlPLC5NoCheck;
    }
    else if ( strcmp( moduleName, "nbv1in" ) == 0 ) {
        devPtr->writeData = ornlPLC5NoWrite;
        devPtr->readData = ornlPLC5Nbv1InReadWord;
        devPtr->readOther = ornlPLC5NoRead;
        devPtr->checkForce = ornlPLC5NoCheck;
    }
    else if ( strcmp( moduleName, "nbv1out" ) == 0 ) {
        devPtr->writeData = ornlPLC5Nbv1OutWriteWord;
        devPtr->readData = ornlPLC5Nbv1OutReadWord;
        devPtr->readOther = ornlPLC5Nbv1OutReadOtherWord;
        devPtr->checkForce = ornlPLC5CheckForceGeneric;
    }
    else if ( strcmp( moduleName, "discrete" ) == 0 ) {
        devPtr->writeData = ornlPLC5WriteBitGeneric;
        devPtr->readData = ornlPLC5ReadBitGeneric;
        devPtr->readOther = ornlPLC5ReadOtherBitGeneric;
        devPtr->checkForce = ornlPLC5CheckForceBitGeneric;
    }
    else if ( strcmp( moduleName, "word" ) == 0 ) {
        devPtr->writeData = ornlPLC5WriteWordGeneric;
        devPtr->readData = ornlPLC5ReadWordGeneric;
        devPtr->readOther = ornlPLC5ReadOtherWordGeneric;
        devPtr->checkForce = ornlPLC5CheckForceGeneric;
    }
    else if ( strcmp( moduleName, "status" ) == 0 ) {
        devPtr->writeData = ornlPLC5WriteWordGeneric;
        devPtr->readData = ornlPLC5ReadWordGeneric;
        devPtr->readOther = ornlPLC5ReadOtherWordGeneric;
        devPtr->checkForce = ornlPLC5CheckForceGeneric;
    }
    else if ( strcmp( moduleName, "bit" ) == 0 ) {
        devPtr->writeData = ornlPLC5WriteBitGeneric;
        devPtr->readData = ornlPLC5ReadBitGeneric;
        devPtr->readOther = ornlPLC5ReadOtherBitGeneric;
        devPtr->checkForce = ornlPLC5CheckForceBitGeneric;
    }
    else if ( strcmp( moduleName, "bitd" ) == 0 ) {
        devPtr->writeData = ornlPLC5WriteBitDirect;
        devPtr->readData = ornlPLC5ReadBitDirect;
        devPtr->readOther = ornlPLC5ReadBitDirect;
        devPtr->checkForce = ornlPLC5NoCheck;
    }
    else if ( strcmp( moduleName, "wordd" ) == 0 ) {
        devPtr->writeData = ornlPLC5WriteWordDirect;
        devPtr->readData = ornlPLC5ReadWordDirect;
        devPtr->readOther = ornlPLC5ReadWordDirect;
        devPtr->checkForce = ornlPLC5NoCheck;
    }
    else {
        devPtr->writeData = ornlPLC5NoWrite;
        devPtr->readData = ornlPLC5NoRead;
        devPtr->readOther = ornlPLC5NoRead;
        devPtr->checkForce = ornlPLC5NoCheck;
    }

#if 0
    printf( "plc5Addr = %s ", plc5Addr );
    printf( "moduleName = %s ", moduleName );
    printf( "channel = %d\n", channel );
#endif

    /* Put addr into dev for all devices to aid debugging during runtime */
    strcpy(devPtr->plc5Addr, plc5Addr); 

    stat = parsePLC5Address(1, plc5Addr, &fileType, &fileNum, &offset, &bit );
    if ( !( stat & 1 ) ) {
        printf( "parsePLC5Address - Bad Address ");
        printf( "plc5Addr = %s ", plc5Addr );
        printf( "moduleName = %s ", moduleName );
        printf( "channel = %d\n", channel );
        return ornlPLC5_BadAddr;
    }

    /**

     * Direct access records don't need their bounds checked. Instead they need to point to
     * the correct plc5 recPtr, which has the socket connection created already by 
     * directThread, and the data structure which contains the PLC IP address etc.
     */
    if ((strcmp(moduleName, "bitd") == 0) || (strcmp(moduleName, "wordd") == 0)) {
        cur = g_plc5RecListHead->flink;
        while ( cur ) {
            if ( strcmp( cur->devName, devName ) == 0 ) {
                if ( cur->unit == unit ) {
                    devPtr->recPtr = cur;
#if 0					
                    printf("ornlPLC5GetDevHandle - plc5Addr %s plc5 %p\n", devPtr->plc5Addr, devPtr->recPtr->plc5);
#endif
                    *handle = (ornlPLC5DevHandle) devPtr;
                    return ornlPLC5_Success;
                }
            }
            cur = cur->flink;
        }
        *handle = (ornlPLC5DevHandle) devPtr;
        return ornlPLC5_BadAddr;
    }


#if 0
    printf( "fileType = %d ", fileType );
    printf( "fileNum = %d ", fileNum );
    printf( "offset = %d ", offset );
    printf( "bit = %d\n", bit );
#endif


    cur = g_plc5RecListHead->flink;

#if 0
    printf( "devName = %s ", devName );
    printf( "cur->devName = %s ", cur->devName );
    printf( "fileNum = %d ", fileNum );
    printf( "cur->plc5FileNum = %d\n", cur->plc5FileNum );
    printf( "unit = %d ", unit );
    printf( "cur->unit = %d ", cur->unit );
    printf( "offset = %d ", offset );
    printf( "cur->plc5BaseOffset = %d ", cur->plc5BaseOffset );
    printf( "cur->plc5MaxOffset = %d\n", cur->plc5MaxOffset );
#endif

    /**
     * Check to make sure the scan block covers the location of this entry
     */

#if 0
    if (strstr(plc5Addr, "T4")) {
        printf( "devName = %s ", devName );
        printf( "cur->devName = %s ", cur->devName );
        printf( "fileNum = %d ", fileNum );
        printf( "cur->plc5FileNum = %d\n", cur->plc5FileNum );
        printf( "unit = %d ", unit );
        printf( "cur->unit = %d ", cur->unit );
        printf( "offset = %d ", offset );
        printf( "cur->plc5BaseOffset = %d ", cur->plc5BaseOffset );
        printf( "cur->plc5MaxOffset = %d\n", cur->plc5MaxOffset );
        printf( "devPtr->dataPtr = %p ", devPtr->dataPtr );
        printf( "devPtr->moduleBase = %d ", devPtr->moduleBase );
        printf( "devPtr->offset = %d\n\n", devPtr->offset );
    }
#endif

    while ( cur ) {

#if 0
        printf("%d ", count++);
#endif

        if ( strcmp( cur->devName, devName ) == 0 ) {

            if ( cur->plc5FileNum == fileNum ) {

                if ( cur->unit == unit ) {

                    if ( ( offset >= cur->plc5BaseOffset ) &&
                            ( offset <= cur->plc5MaxOffset ) ) {

                        devPtr->dataPtr = &cur->xferBlock;
                        devPtr->moduleBase = offset - cur->plc5BaseOffset;
                        devPtr->offset = devPtr->moduleBase;
                        devPtr->setMask = ( (unsigned short ) 1 ) << bit;
                        devPtr->clrMask = ~devPtr->setMask;

#if 0
                        if (strstr(plc5Addr, "T4")) {
                            printf( "devName = %s ", devName );
                            printf( "cur->devName = %s ", cur->devName );
                            printf( "fileNum = %d ", fileNum );
                            printf( "cur->plc5FileNum = %d\n", cur->plc5FileNum );
                            printf( "unit = %d ", unit );
                            printf( "cur->unit = %d ", cur->unit );
                            printf( "offset = %d ", offset );
                            printf( "cur->plc5BaseOffset = %d ", cur->plc5BaseOffset );
                            printf( "cur->plc5MaxOffset = %d\n", cur->plc5MaxOffset );
                            printf( "devPtr->dataPtr = %p ", devPtr->dataPtr );
                            printf( "devPtr->moduleBase = %d ", devPtr->moduleBase );
                            printf( "devPtr->offset = %d\n\n", devPtr->offset );
                        }
#endif
                        if ( strcmp( moduleName, "ofeBin" ) == 0 ) {
                            devPtr->offset += channel - 1;
                            devPtr->controlWordOffset = devPtr->moduleBase + 4;
                            devPtr->polaritySetMask =
                                ( (unsigned short ) 1 ) << ( channel - 1 );
                            devPtr->polarityClrMask = ~devPtr->polaritySetMask;
                        }
                        else if ( strcmp( moduleName, "ifeBin" ) == 0 ) {
                            devPtr->offset += channel + 3;
                            devPtr->polaritySetMask =
                                ( (unsigned short ) 1 ) << ( channel - 1 );
                        }
                        else if ( strcmp( moduleName, "nov" ) == 0 ) {
                            devPtr->offset += channel;
                        }
                        else if ( strcmp( moduleName, "niv" ) == 0 ) {
                            devPtr->offset += 5 + ( channel - 1 ) * 2;
                        }
                        else if ( strcmp( moduleName, "nt1" ) == 0 ) {
                            if ( channel < 9 ) {
                                devPtr->offset += 5 + ( channel - 1 ) * 2;
                            }
                            else if ( channel == 9 ) { /* cold junc temp */
                                devPtr->offset += 3;
                            }
                        }
                        /* output channels go from 1 to 2 */
                        else if ( strcmp( moduleName, "nbv1out" ) == 0 ) {
                            if ( channel < 1 ) channel = 1;
                            if ( channel > 2 ) channel = 2;
                            devPtr->offset += channel;
                        }
                        /* input channels go from 3 to 8 */
                        else if ( strcmp( moduleName, "nbv1in" ) == 0 ) {
                            if ( channel < 3 ) channel = 3;
                            if ( channel > 8 ) channel = 8;
                            devPtr->offset += 9 + ( channel - 3 ) * 2;
                        }
                        else if ( strcmp( moduleName, "bitd" ) == 0 ) {
                            printf("bitd type has no offsets\n");
                        }

                        *handle = (ornlPLC5DevHandle) devPtr;
                        return ornlPLC5_Success;

                    } else {

                        printf( "offset out of bounds - 1\n" );

                        printf( "moduleName = %s ", moduleName );
                        printf( "channel = %d ", channel );
                        printf( "plc5Addr = %s ", plc5Addr );
                        printf( "fileType = %d ", fileType );
                        printf( "fileNum = %d ", fileNum );
                        printf( "offset = %d ", offset );
                        printf( "bit = %d\n\n", bit );

                        printf( "devName = %s ", devName );
                        printf( "cur->devName = %s ", cur->devName );
                        printf( "fileNum = %d ", fileNum );
                        printf( "cur->plc5FileNum = %d\n", cur->plc5FileNum );
                        printf( "unit = %d ", unit );
                        printf( "cur->unit = %d ", cur->unit );
                        printf( "offset = %d ", offset );
                        printf( "cur->plc5BaseOffset = %d ", cur->plc5BaseOffset );
                        printf( "cur->plc5MaxOffset = %d\n", cur->plc5MaxOffset );
                        printf( "devPtr->dataPtr = %p ", devPtr->dataPtr );
                        printf( "devPtr->moduleBase = %d ", devPtr->moduleBase );
                        printf( "devPtr->offset = %d\n\n", devPtr->offset );

                        devPtr->writeData = ornlPLC5NoWrite;
                        devPtr->readData = ornlPLC5NoRead;
                        devPtr->dataPtr = NULL;
                        devPtr->moduleBase = 0;
                        devPtr->offset = 0;
                        devPtr->setMask = 0;
                        devPtr->clrMask = ~devPtr->setMask;
                        return ornlPLC5_BadAddr;

                    }

                }

            }

        }

        cur = cur->flink;

    }

    printf( "offset out of bounds - 2 - %d %s\n", channel, plc5Addr );

    *handle = (ornlPLC5DevHandle) devPtr;
    return ornlPLC5_BadAddr;

}

/* Module independent code */

int ornlPLC5ReadWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    int stat;

    if ( !devPtr ) return ornlPLC5_Failure;

    stat = devPtr->readData( handle,  value );

    return stat;

}

int ornlPLC5ReadOtherWord (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    int stat;

    if ( !devPtr ) return ornlPLC5_Failure;

    stat = devPtr->readOther( handle,  value );

    return stat;

}

int ornlPLC5CheckForce (
        ornlPLC5DevHandle handle
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    int stat;

    if ( !devPtr ) return 0;

    stat = devPtr->checkForce( handle );

    return stat;

}

int ornlPLC5ReadBit (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    int stat;

    if ( !devPtr ) return ornlPLC5_Failure;

    stat = devPtr->readData( handle,  value );

    return stat;

}

int ornlPLC5ReadOtherBit (
        ornlPLC5DevHandle handle,
        int *value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    int stat;

    if ( !devPtr ) return ornlPLC5_Failure;

    stat = devPtr->readOther( handle,  value );

    return stat;

}

int ornlPLC5WriteWord (
        ornlPLC5DevHandle handle,
        int value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    int stat;

#if 0
    printf( "ornlPLC5WriteWord\n");
#endif

    if ( !devPtr ) return ornlPLC5_Failure;

    stat = devPtr->writeData( handle,  value );

    return stat;

}

int ornlPLC5WriteBit (
        ornlPLC5DevHandle handle,
        int value
        ) {

    ornlPLC5DevPtr devPtr = (ornlPLC5DevPtr) handle;
    int stat;

#if 0
    printf( "ornlPLC5WriteBit %p %d\n", handle, value);
#endif

    if ( !devPtr ) return ornlPLC5_Failure;

    stat = devPtr->writeData( handle,  value );

    return stat;

}

/* Module initialization/configuration code */

static int ornlPLC5WriteNivConfig (
        char *plcIpAddr,
        char *plcConfigAddr,
        int numWords,
        nivConfigPtr nivConfig
        ) {

    enetPlc5Comm plc5;
    int i, ii, retStat, stat;
    short buf[59];

    retStat = ornlPLC5_Success;

    if ( numWords > 59 ) numWords = 59;

    memset( (void *) buf, 0, 59*2 );

    buf[0] = (unsigned short) 0x8800;

    if ( nivConfig->dataFormat < 0 ) nivConfig->dataFormat = 0;
    if ( nivConfig->dataFormat > 1 ) nivConfig->dataFormat = 1;
    buf[1] = ( (short) nivConfig->dataFormat ) << 2;

    buf[2] = 0;

    for ( ii=0, i=3; ii<8; ii++ ) {
        buf[i++] = (short) nivConfig->minScale[ii];
        buf[i++] = (short) nivConfig->maxScale[ii];
        buf[i++] = 0;                  /* low alarm */
        buf[i++] = 0;                  /* high alarm */
        buf[i++] = 0;                  /* rate alarm / alarm enable */
        buf[i++] = ( (short) nivConfig->filter[ii] ) << 8;
        buf[i++] = 0;                  /* T/C, RTD data */
    }

    stat = enetPlc5Init( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        retStat = stat;
        goto errReturn;
    }

    enetPlc5SetTimeout( plc5, 10.0 );

    stat = enetPlc5ConnectPlc( plc5, plcIpAddr );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
        retStat = stat;
        goto errReturn;
    }

    stat = enetPlc5WritePlc( plc5, plcConfigAddr, numWords, buf, NULL );
    if ( !( stat & 1 ) ) {
        retStat = stat;
    }

    stat = enetPlc5DisconnectPlc( plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
    }

    stat = enetPlc5Destroy( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
    }

errReturn:

    return retStat;

}

static int ornlPLC5WriteNt1Config (
        char *plcIpAddr,
        char *plcConfigAddr,
        int numWords,
        nt1ConfigPtr nt1Config
        ) {

    enetPlc5Comm plc5;
    int i, ii, retStat, stat;
    short buf[59];

    retStat = ornlPLC5_Success;

    if ( numWords > 59 ) numWords = 59;

    memset( (void *) buf, 0, 59*2 );

    buf[0] = (unsigned short) 0x8800;

    buf[1] = 0;
    buf[1] |= (short) ( ( nt1Config->dataFormat & 1 ) << 2 );
    buf[1] |= (short) ( ( nt1Config->tempScale & 1 ) << 1 );

    buf[2] = 0;

    for ( ii=0, i=3; ii<8; ii++ ) {
        buf[i++] = (short) nt1Config->minScale[ii];
        buf[i++] = (short) nt1Config->maxScale[ii];
        buf[i++] = 0;                  /* low alarm */
        buf[i++] = 0;                  /* high alarm */
        buf[i++] = 0;                  /* rate alarm / alarm enable */
        buf[i++] = ( (short) nt1Config->filter[ii] ) << 8;
        buf[i++] = (short) ( ( nt1Config->tcType[ii] & 0xf ) << 12 );
    }

    stat = enetPlc5Init( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        retStat = stat;
        goto errReturn;
    }

    enetPlc5SetTimeout( plc5, 10.0 );

    stat = enetPlc5ConnectPlc( plc5, plcIpAddr );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
        retStat = stat;
        goto errReturn;
    }

    stat = enetPlc5WritePlc( plc5, plcConfigAddr, numWords, buf, NULL );
    if ( !( stat & 1 ) ) {
        retStat = stat;
    }

    stat = enetPlc5DisconnectPlc( plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
    }

    stat = enetPlc5Destroy( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
    }

errReturn:

    return retStat;

}

static int ornlPLC5WriteNbv1Config (
        char *plcIpAddr,
        char *plcConfigAddr,
        int numWords,
        nbv1ConfigPtr nbv1Config
        ) {

    enetPlc5Comm plc5;
    int i, ii, retStat, stat;
    short buf[59];

    retStat = ornlPLC5_Success;

    stat = enetPlc5Init( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        retStat = stat;
        goto errReturn;
    }

    enetPlc5SetTimeout( plc5, 10.0 );

    stat = enetPlc5ConnectPlc( plc5, plcIpAddr );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
        retStat = stat;
        goto errReturn;
    }

    if ( numWords > 59 ) numWords = 59;

    stat = enetPlc5ReadPlc( plc5, plcConfigAddr, numWords, buf, NULL );
    if ( !( stat & 1 ) ) {

        retStat = stat;

        stat = enetPlc5DisconnectPlc( plc5 );
        if ( !( stat & 1 ) ) {
            if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
        }

        stat = enetPlc5Destroy( &plc5 );
        if ( !( stat & 1 ) ) {
            if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
        }

        goto errReturn;

    }

    buf[0] = (unsigned short) 0x8820;

    /* don't touch buf[1] - buf[2] */
    for ( i=3; i<59; i++ ) {
        buf[i] = 0;
    }

    buf[3] = 0;
    buf[3] |= (short) ( ( nbv1Config->dataFormat & 1 ) << 2 );
    buf[3] |= (short) ( ( nbv1Config->tempScale & 1 ) << 1 );

    buf[4] = 0;

    /* two outputs */
    for ( ii=0, i=5; ii<2; ii++ ) {
        buf[i++] = (short) nbv1Config->minScale[ii];
        buf[i++] = (short) nbv1Config->maxScale[ii];
        buf[i++] = (short) nbv1Config->minClamp[ii];
        buf[i++] = (short) nbv1Config->maxClamp[ii];
        buf[i] = (short) ( nbv1Config->resetState[ii] & 3 ) << 13;
        buf[i++] |= ( (short) nbv1Config->maxRampRate[ii] & 0xfff );
        buf[i++] = (short) nbv1Config->resetValue[ii];
    }

    /* six inputs */
    for ( ii=2, i=17; ii<8; ii++ ) {
        buf[i++] = (short) nbv1Config->minScale[ii];
        buf[i++] = (short) nbv1Config->maxScale[ii];
        buf[i++] = 0;                  /* low alarm */
        buf[i++] = 0;                  /* high alarm */
        buf[i++] = 0;                  /* rate alarm / alarm enable */
        buf[i++] = ( (short) nbv1Config->filter[ii] ) << 8;
        buf[i++] = (short) ( ( nbv1Config->tcType[ii] & 0xf ) << 12 );
    }

    stat = enetPlc5Init( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        retStat = stat;
        goto errReturn;
    }

    enetPlc5SetTimeout( plc5, 10.0 );

    stat = enetPlc5ConnectPlc( plc5, plcIpAddr );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
        retStat = stat;
        goto errReturn;
    }

    stat = enetPlc5WritePlc( plc5, plcConfigAddr, numWords, buf, NULL );
    if ( !( stat & 1 ) ) {
        retStat = stat;
    }

    stat = enetPlc5DisconnectPlc( plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
    }

    stat = enetPlc5Destroy( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
    }

errReturn:

    return retStat;

}

static int ornlPLC5WriteNovConfig (
        char *plcIpAddr,
        char *plcConfigAddr,
        int numWords,
        novConfigPtr novConfig
        ) {

    enetPlc5Comm plc5;
    int i, ii, retStat, stat;
    short buf[59];

    retStat = ornlPLC5_Success;

    stat = enetPlc5Init( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        retStat = stat;
        goto errReturn;
    }

    enetPlc5SetTimeout( plc5, 10.0 );

    stat = enetPlc5ConnectPlc( plc5, plcIpAddr );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
        retStat = stat;
        goto errReturn;
    }

    if ( numWords > 59 ) numWords = 59;

    stat = enetPlc5ReadPlc( plc5, plcConfigAddr, numWords, buf, NULL );
    if ( !( stat & 1 ) ) {

        retStat = stat;

        stat = enetPlc5DisconnectPlc( plc5 );
        if ( !( stat & 1 ) ) {
            if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
        }

        stat = enetPlc5Destroy( &plc5 );
        if ( !( stat & 1 ) ) {
            if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
        }

        goto errReturn;

    }

    buf[0] = (unsigned short) 0x8880;

    /* don't touch buf[1] - buf[8] */
    for ( i=9; i<59; i++ ) {
        buf[i] = 0;
    }

    if ( novConfig->dataFormat < 0 ) novConfig->dataFormat = 0;
    if ( novConfig->dataFormat > 1 ) novConfig->dataFormat = 1;
    buf[9] = ( (short) novConfig->dataFormat ) << 2;

    buf[10] = 0;

    for ( ii=0, i=11; ii<8; ii++ ) {
        buf[i++] = (short) novConfig->minScale[ii];
        buf[i++] = (short) novConfig->maxScale[ii];
        buf[i++] = (short) novConfig->minClamp[ii];
        buf[i++] = (short) novConfig->maxClamp[ii];
        buf[i] = (short) ( novConfig->resetState[ii] & 3 ) << 13;
        buf[i++] |= ( (short) novConfig->maxRampRate[ii] & 0xfff );
        buf[i++] = (short) novConfig->resetValue[ii];
    }

    stat = enetPlc5Init( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        retStat = stat;
        goto errReturn;
    }

    enetPlc5SetTimeout( plc5, 10.0 );

    stat = enetPlc5ConnectPlc( plc5, plcIpAddr );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
        retStat = stat;
        goto errReturn;
    }

    stat = enetPlc5WritePlc( plc5, plcConfigAddr, numWords, buf, NULL );
    if ( !( stat & 1 ) ) {
        retStat = stat;
    }

    stat = enetPlc5DisconnectPlc( plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
    }

    stat = enetPlc5Destroy( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
    }

errReturn:

    return retStat;

}

static int ornlPLC5WriteIfeConfig (
        char *plcIpAddr,
        char *plcConfigAddr,
        int numWords,
        ifeConfigPtr ifeConfig
        ) {

    enetPlc5Comm plc5;
    int i, ii, retStat, stat;
    short buf[37];
    int minScalePolarity, maxScalePolarity;

    retStat = ornlPLC5_Success;

    if ( numWords > 37 ) numWords = 37;

    memset( (void *) buf, 0, 37*2 );

    buf[0] = 0; /* range, channels 1 - 8 */
    for ( i=0; i<8; i++ ) {
        buf[0] |= (short) ( ( ifeConfig->range[i] & 3 ) << i*2 );
    }

    buf[1] = 0; /* range, channels 9 - 16 */
    for ( i=0; i<8; i++ ) {
        buf[1] |= (short) ( ( ifeConfig->range[i+8] & 3 ) << i*2 );
    }

    buf[2] = 0;
    buf[2] |= (short) ( ( ifeConfig->dataFormat & 3 ) << 9 );
    buf[2] |= (short) ( ( ifeConfig->inputType & 1 ) << 8 );
    buf[2] |= (short) ( ifeConfig->filter & 0xff );

    minScalePolarity = 0;
    maxScalePolarity = 0;

    for ( ii=0, i=5; ii<16; ii++ ) {
        if ( ifeConfig->minScale[ii] < 0 ) minScalePolarity |=
            ( (unsigned short) 1 << ii );
        if ( ifeConfig->maxScale[ii] < 0 ) maxScalePolarity |=
            ( (unsigned short) 1 << ii );
        buf[i++] = (short) bcd( ifeConfig->minScale[ii] );
        buf[i++] = (short) bcd( ifeConfig->maxScale[ii] );
    }

    buf[3] = minScalePolarity;
    buf[4] = maxScalePolarity;

    stat = enetPlc5Init( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        retStat = stat;
        goto errReturn;
    }

    enetPlc5SetTimeout( plc5, 10.0 );

    stat = enetPlc5ConnectPlc( plc5, plcIpAddr );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
        retStat = stat;
        goto errReturn;
    }

    stat = enetPlc5WritePlc( plc5, plcConfigAddr, numWords, buf, NULL );
    if ( !( stat & 1 ) ) {
        retStat = stat;
    }

    stat = enetPlc5DisconnectPlc( plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
    }

    stat = enetPlc5Destroy( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
    }

errReturn:

    return retStat;

}

static int ornlPLC5WriteOfeConfig (
        char *plcIpAddr,
        char *plcConfigAddr,
        int numWords,
        ofeConfigPtr ofeConfig
        ) {

    enetPlc5Comm plc5;
    int i, ii, retStat, stat;
    short buf[13];
    unsigned short minMaxScalePolarity;

    retStat = ornlPLC5_Success;

    stat = enetPlc5Init( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        retStat = stat;
        goto errReturn;
    }

    enetPlc5SetTimeout( plc5, 10.0 );

    stat = enetPlc5ConnectPlc( plc5, plcIpAddr );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
        retStat = stat;
        goto errReturn;
    }

    if ( numWords > 13 ) numWords = 13;

    stat = enetPlc5ReadPlc( plc5, plcConfigAddr, numWords, buf, NULL );
    if ( !( stat & 1 ) ) {

        retStat = stat;

        stat = enetPlc5DisconnectPlc( plc5 );
        if ( !( stat & 1 ) ) {
            if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
        }

        stat = enetPlc5Destroy( &plc5 );
        if ( !( stat & 1 ) ) {
            if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
        }

        goto errReturn;

    }

    /* don't touch buf[0] - buf[3] and first 4 bits of buf[4] */
    buf[4] &= 0xF;
    for ( i=5; i<13; i++ ) {
        buf[i] = 0;
    }

    if ( ofeConfig->dataFormat < 0 ) ofeConfig->dataFormat = 0;
    if ( ofeConfig->dataFormat > 1 ) ofeConfig->dataFormat = 1;
    buf[4] |= ( (short) ofeConfig->dataFormat ) << 15;

    minMaxScalePolarity = 0;

    for ( ii=0, i=5; ii<4; ii++ ) {

        if ( ofeConfig->minScale[ii] < 0 ) minMaxScalePolarity |=
            ( (unsigned short) 1 << (i-1) );

        buf[i++] = (short) abs( ofeConfig->minScale[ii] );

        if ( ofeConfig->maxScale[ii] < 0 ) minMaxScalePolarity |=
            ( (unsigned short) 1 << (i-1) );

        buf[i++] = (short) abs( ofeConfig->maxScale[ii] );

    }

    buf[4] |= minMaxScalePolarity;

    stat = enetPlc5WritePlc( plc5, plcConfigAddr, numWords, buf, NULL );
    if ( !( stat & 1 ) ) {
        retStat = stat;
    }

    stat = enetPlc5DisconnectPlc( plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
    }

    stat = enetPlc5Destroy( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
    }

errReturn:

    return retStat;

}

static int ornlPLC5CopyRegion (
        char *plcIpAddr,
        char *plcFromAddr,
        char *plcToAddr,
        int numWords
        ) {

    enetPlc5Comm plc5;
    int retStat, stat;
    short buf[2000];

    printf("ornlPLC5CopyRegion\n");

    retStat = ornlPLC5_Success;

    if ( numWords > 2000 ) {
        retStat = ornlPLC5_Failure;
        goto errReturn;
    }

    stat = enetPlc5Init( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
        retStat = stat;
        goto errReturn;
    }

    enetPlc5SetTimeout( plc5, 10.0 );

    stat = enetPlc5ConnectPlc( plc5, plcIpAddr );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
        retStat = stat;
        goto errReturn;
    }

    stat = enetPlc5ReadPlc( plc5, plcFromAddr, numWords, buf, NULL );
    if ( !( stat & 1 ) ) {

        retStat = stat;

        stat = enetPlc5DisconnectPlc( plc5 );
        if ( !( stat & 1 ) ) {
            if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
        }

        stat = enetPlc5Destroy( &plc5 );
        if ( !( stat & 1 ) ) {
            if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
        }

        goto errReturn;

    }

    stat = enetPlc5WritePlc( plc5, plcToAddr, numWords, buf, NULL );
    if ( !( stat & 1 ) ) {
        retStat = stat;
    }

    stat = enetPlc5DisconnectPlc( plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
    }

    stat = enetPlc5Destroy( &plc5 );
    if ( !( stat & 1 ) ) {
        if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
    }

errReturn:

    return retStat;

}

#define ERR_OK			1
#define ERR_FAIL		100
#define ERR_UNKNOWN_DEV_TYPE	102
#define ERR_SYNTAX		104
#define ERR_NOMEM		106
#define ERR_EOF			108
#define ERR_UNKNOWN_KEYWORD	110
#define ERR_NOINIT		112

#define COMMENT_CHAR '#'

#define DONE						-1
#define GETTING_DEV_TYPE				1
#define GETTING_DEV_NAME				2
#define GETTING_DEV_OPEN_BRACE				3
#define GETTING_DEV_UNIT_OR_CLOSE_BRACE			4
#define GETTING_UNIT_OPEN_BRACE				5
#define GETTING_UNIT_KEYWORDS_OR_CLOSE_BRACE		6
#define GETTING_UNIT_KEYWORD_VALUE			7
#define GETTING_UNIT_KEYWORDS_OR_COLON_OR_CLOSE_BRACE	8
#define GETTING_UNIT_KEYWORD_2ND_VALUE			9

static char g_buf[511+1], *g_tk;
static int g_need_to_read_file;
static int g_line = 0;

static int readFile (
        FILE *inFile
        ) {

    int i, l, blank = 1;
    char *ptr;

    do {

        ptr = fgets( g_buf, 511, inFile );
        if ( !ptr ) return ERR_EOF;

        g_line++;

        l = strlen(g_buf);
        if ( g_buf[l-1] == '\n' ) {
            g_buf[l-1] = 0;
            l--;
        }

        for ( i=0; i<l; i++ ) {

            if ( g_buf[i] == COMMENT_CHAR ) {
                g_buf[i] = 0;
                l = i;
                break;
            }

        }

        for ( i=0; i<l; i++ ) {

            if ( !isspace( (int) g_buf[i] ) ) {
                blank = 0;
                break;
            }

        }

    } while ( blank );

    return ERR_OK;

}

static int nextToken (
        FILE *inFile
        ) {

    int stat;

    do {

        if ( g_need_to_read_file ) {

            stat = readFile( inFile );
            if ( !( stat & 1 ) ) return stat;
            g_need_to_read_file = 0;

            g_tk = strtok( g_buf, "[] \t\n" );
            if ( !g_tk ) g_need_to_read_file = 1;

        }
        else {

            g_tk = strtok( NULL, "[] \t\n" );
            if ( !g_tk ) {
                /* printf( "\n" ); */
                g_need_to_read_file = 1;
            }

        }

    } while ( !g_tk );

    return ERR_OK;

}

static int configOfe (
        char *ipAddr,
        FILE *inFile
        ) {

    int i, stat, more, numWords;
    char plc5ConfigAddr[31+1];
    ofeConfigType ofeConfig;

    int maxChannels = 4;

    numWords = 0;
    strcpy( plc5ConfigAddr, "" );
    memset( (void *) &ofeConfig, 0, sizeof(ofeConfigType) );

    more = 1;
    do {

        stat = nextToken( inFile );
        if ( stat & 1 ) {

            /* printf( "[%s]\n", g_tk ); */

            if ( strcmp( g_tk, "addr" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                strncpy( plc5ConfigAddr, g_tk, 31 );
                plc5ConfigAddr[31] = 0;

            }
            else if ( strcmp( g_tk, "numwords" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                numWords = atol( g_tk );

            }
            else if ( strcmp( g_tk, "dataformat" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                ofeConfig.dataFormat = atol( g_tk );

            }
            else if ( strcmp( g_tk, "scale" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configOfe, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                ofeConfig.minScale[i-1] = atol( g_tk );
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                ofeConfig.maxScale[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "}" ) == 0 ) {

                more = 0;

            }
            else {

                printf( "configOfe, unknown keyword: [%s]\n", g_tk );
                return ERR_FAIL;

            }

        }
        else {

            goto errReturn;

        }

    } while ( more );

    stat = ornlPLC5WriteOfeConfig( ipAddr, plc5ConfigAddr, numWords,
            &ofeConfig );

    return stat;

errReturn:

    printf( "configOfe, unexpected end of file\n" );
    return ERR_FAIL;

}

static int configNiv (
        char *ipAddr,
        FILE *inFile
        ) {

    int i, stat, more, numWords;
    char plc5ConfigAddr[31+1];
    nivConfigType nivConfig;

    int maxChannels = 8;

    numWords = 0;
    strcpy( plc5ConfigAddr, "" );
    memset( (void *) &nivConfig, 0, sizeof(nivConfigType) );

    more = 1;
    do {

        stat = nextToken( inFile );
        if ( stat & 1 ) {

            /* printf( "[%s]\n", g_tk ); */

            if ( strcmp( g_tk, "addr" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                strncpy( plc5ConfigAddr, g_tk, 31 );
                plc5ConfigAddr[31] = 0;

            }
            else if ( strcmp( g_tk, "numwords" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                numWords = atol( g_tk );

            }
            else if ( strcmp( g_tk, "dataformat" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nivConfig.dataFormat = atol( g_tk );

            }
            else if ( strcmp( g_tk, "scale" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configNiv, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nivConfig.minScale[i-1] = atol( g_tk );
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nivConfig.maxScale[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "filter" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configNiv, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nivConfig.filter[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "}" ) == 0 ) {

                more = 0;

            }
            else {

                printf( "configNiv, unknown keyword: [%s]\n", g_tk );
                return ERR_FAIL;

            }

        }
        else {

            goto errReturn;

        }

    } while ( more );

    stat = ornlPLC5WriteNivConfig( ipAddr, plc5ConfigAddr, numWords,
            &nivConfig );

    return stat;

errReturn:

    printf( "configNiv, unexpected end of file\n" );
    return ERR_FAIL;

}

static int configNt1 (
        char *ipAddr,
        FILE *inFile
        ) {

    int i, stat, more, numWords;
    char plc5ConfigAddr[31+1];
    nt1ConfigType nt1Config;

    int maxChannels = 8;

    numWords = 0;
    strcpy( plc5ConfigAddr, "" );
    memset( (void *) &nt1Config, 0, sizeof(nt1ConfigType) );

    more = 1;
    do {

        stat = nextToken( inFile );
        if ( stat & 1 ) {

            /* printf( "[%s]\n", g_tk ); */

            if ( strcmp( g_tk, "addr" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                strncpy( plc5ConfigAddr, g_tk, 31 );
                plc5ConfigAddr[31] = 0;

            }
            else if ( strcmp( g_tk, "numwords" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                numWords = atol( g_tk );

            }
            else if ( strcmp( g_tk, "dataformat" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nt1Config.dataFormat = atol( g_tk );

            }
            else if ( strcmp( g_tk, "tempScale" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nt1Config.tempScale = atol( g_tk );

            }
            else if ( strcmp( g_tk, "scale" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configNt1, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nt1Config.minScale[i-1] = atol( g_tk );
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nt1Config.maxScale[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "filter" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configNt1, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nt1Config.filter[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "tctype" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configNt1, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nt1Config.tcType[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "}" ) == 0 ) {

                more = 0;

            }
            else {

                printf( "configNt1, unknown keyword: [%s]\n", g_tk );
                return ERR_FAIL;

            }

        }
        else {

            goto errReturn;

        }

    } while ( more );

    stat = ornlPLC5WriteNt1Config( ipAddr, plc5ConfigAddr, numWords,
            &nt1Config );

    return stat;

errReturn:

    printf( "configNt1, unexpected end of file\n" );
    return ERR_FAIL;

}

static int configNov (
        char *ipAddr,
        FILE *inFile
        ) {

    int i, stat, more, numWords;
    char plc5ConfigAddr[31+1];
    novConfigType novConfig;

    int maxChannels = 8;

    numWords = 0;
    strcpy( plc5ConfigAddr, "" );
    memset( (void *) &novConfig, 0, sizeof(novConfigType) );

    more = 1;
    do {

        stat = nextToken( inFile );
        if ( stat & 1 ) {

            /* printf( "[%s]\n", g_tk ); */

            if ( strcmp( g_tk, "addr" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                strncpy( plc5ConfigAddr, g_tk, 31 );
                plc5ConfigAddr[31] = 0;

            }
            else if ( strcmp( g_tk, "numwords" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                numWords = atol( g_tk );

            }
            else if ( strcmp( g_tk, "dataformat" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                novConfig.dataFormat = atol( g_tk );

            }
            else if ( strcmp( g_tk, "scale" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configNov, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                novConfig.minScale[i-1] = atol( g_tk );
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                novConfig.maxScale[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "clamp" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configNov, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                novConfig.minClamp[i-1] = atol( g_tk );
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                novConfig.maxClamp[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "maxramprate" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configNov, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                novConfig.maxRampRate[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "resetstate" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configNov, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                novConfig.resetState[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "resetvalue" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configNov, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                novConfig.resetValue[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "}" ) == 0 ) {

                more = 0;

            }
            else {

                printf( "confignov, unknown keyword: [%s]\n", g_tk );
                return ERR_FAIL;

            }

        }
        else {

            goto errReturn;

        }

    } while ( more );

    stat = ornlPLC5WriteNovConfig( ipAddr, plc5ConfigAddr, numWords,
            &novConfig );

    return stat;

errReturn:

    printf( "configNov, unexpected end of file\n" );
    return ERR_FAIL;

}

static int configNbv1 (
        char *ipAddr,
        FILE *inFile
        ) {

    int i, stat, more, numWords;
    char plc5ConfigAddr[31+1];
    nbv1ConfigType nbv1Config;

    int minOutChannel = 1;
    int maxOutChannel = 2;
    int minInChannel = 3;
    int maxInChannel = 8;

    numWords = 0;
    strcpy( plc5ConfigAddr, "" );
    memset( (void *) &nbv1Config, 0, sizeof(nbv1ConfigType) );

    more = 1;
    do {

        stat = nextToken( inFile );
        if ( stat & 1 ) {

            /* printf( "[%s]\n", g_tk ); */

            if ( strcmp( g_tk, "addr" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                strncpy( plc5ConfigAddr, g_tk, 31 );
                plc5ConfigAddr[31] = 0;

            }
            else if ( strcmp( g_tk, "numwords" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                numWords = atol( g_tk );

            }
            else if ( strcmp( g_tk, "dataformat" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nbv1Config.dataFormat = atol( g_tk );

            }
            else if ( strcmp( g_tk, "tempScale" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nbv1Config.tempScale = atol( g_tk );

            }
            else if ( strcmp( g_tk, "scale" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < minOutChannel ) || ( i > maxInChannel ) ) {
                    printf( "configNbv1, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nbv1Config.minScale[i-1] = atol( g_tk );
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nbv1Config.maxScale[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "filter" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < minInChannel ) || ( i > maxInChannel ) ) {
                    printf( "configNbv1, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nbv1Config.filter[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "tctype" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < minInChannel ) || ( i > maxInChannel ) ) {
                    printf( "configNbv1, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nbv1Config.tcType[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "clamp" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < minOutChannel ) || ( i > maxOutChannel ) ) {
                    printf( "configNbv1, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nbv1Config.minClamp[i-1] = atol( g_tk );
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nbv1Config.maxClamp[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "maxramprate" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < minOutChannel ) || ( i > maxOutChannel ) ) {
                    printf( "configNbv1, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nbv1Config.maxRampRate[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "resetstate" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < minOutChannel ) || ( i > maxOutChannel ) ) {
                    printf( "configNbv1, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nbv1Config.resetState[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "resetvalue" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < minOutChannel ) || ( i > maxOutChannel ) ) {
                    printf( "configNbv1, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                nbv1Config.resetValue[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "}" ) == 0 ) {

                more = 0;

            }
            else {

                printf( "confignbv1, unknown keyword: [%s]\n", g_tk );
                return ERR_FAIL;

            }

        }
        else {

            goto errReturn;

        }

    } while ( more );

    stat = ornlPLC5WriteNbv1Config( ipAddr, plc5ConfigAddr, numWords,
            &nbv1Config );

    return stat;

errReturn:

    printf( "configNbv1, unexpected end of file\n" );
    return ERR_FAIL;

}

static int configIfe (
        char *ipAddr,
        FILE *inFile
        ) {

    int i, stat, more, numWords;
    char plc5ConfigAddr[31+1];
    ifeConfigType ifeConfig;

    int maxChannels = 16;

    numWords = 0;
    strcpy( plc5ConfigAddr, "" );
    memset( (void *) &ifeConfig, 0, sizeof(ifeConfigType) );

    more = 1;
    do {

        stat = nextToken( inFile );
        if ( stat & 1 ) {

            /* printf( "[%s]\n", g_tk ); */

            if ( strcmp( g_tk, "addr" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                strncpy( plc5ConfigAddr, g_tk, 31 );
                plc5ConfigAddr[31] = 0;

            }
            else if ( strcmp( g_tk, "numwords" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                numWords = atol( g_tk );

            }
            else if ( strcmp( g_tk, "dataformat" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                ifeConfig.dataFormat = atol( g_tk );

            }
            else if ( strcmp( g_tk, "inputtype" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                ifeConfig.inputType = atol( g_tk );

            }
            else if ( strcmp( g_tk, "filter" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                ifeConfig.filter = atol( g_tk );

            }
            else if ( strcmp( g_tk, "scale" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configIfe, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                ifeConfig.minScale[i-1] = atol( g_tk );
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                ifeConfig.maxScale[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "range" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                i = atol( g_tk );
                if ( ( i < 1 ) || ( i > maxChannels ) ) {
                    printf( "configIfe, bad channel number: [%s]\n", g_tk );
                    return ERR_FAIL;
                }
                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                ifeConfig.range[i-1] = atol( g_tk );

            }
            else if ( strcmp( g_tk, "}" ) == 0 ) {

                more = 0;

            }
            else {

                printf( "configIfe, unknown keyword: [%s]\n", g_tk );
                return ERR_FAIL;

            }

        }
        else {

            goto errReturn;

        }

    } while ( more );

    stat = ornlPLC5WriteIfeConfig( ipAddr, plc5ConfigAddr, numWords,
            &ifeConfig );

    return stat;

errReturn:

    printf( "configIfe, unexpected end of file\n" );
    return ERR_FAIL;

}

static int copyRegion (
        char *ipAddr,
        FILE *inFile
        ) {

    int stat, more, numWords;
    char plc5FromAddr[31+1];
    char plc5ToAddr[31+1];

    numWords = 0;
    strcpy( plc5FromAddr, "" );
    strcpy( plc5ToAddr, "" );

    more = 1;
    do {

        stat = nextToken( inFile );
        if ( stat & 1 ) {

            /* printf( "[%s]\n", g_tk ); */

            if ( strcmp( g_tk, "from" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                strncpy( plc5FromAddr, g_tk, 31 );
                plc5FromAddr[31] = 0;

            }
            else if ( strcmp( g_tk, "to" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                strncpy( plc5ToAddr, g_tk, 31 );
                plc5ToAddr[31] = 0;

            }
            else if ( strcmp( g_tk, "numwords" ) == 0 ) {

                stat = nextToken( inFile );
                if ( !( stat & 1 ) ) goto errReturn;
                numWords = atol( g_tk );

            }
            else if ( strcmp( g_tk, "}" ) == 0 ) {

                more = 0;

            }
            else {

                printf( "copyRegion, unknown keyword: [%s]\n", g_tk );
                return ERR_FAIL;

            }

        }
        else {

            goto errReturn;

        }

    } while ( more );

    stat = ornlPLC5CopyRegion( ipAddr, plc5FromAddr, plc5ToAddr, numWords );

    return stat;

errReturn:

    printf( "copyRegion, unexpected end of file\n" );
    return ERR_FAIL;

}

int configPLC5Modules (
        char *fileName
        ) {

    FILE *inFile;
    char plc5IpAddr[31+1];
    int stat, findIpAddr;

    inFile = fopen( fileName, "r" );
    if ( !inFile ) {
        perror( "File open failure" );
        return ERR_FAIL;
    }

    /* must have ipaddr first */
    findIpAddr = 1;
    do {

        stat = nextToken( inFile );
        if ( stat & 1 ) {
            if ( strcmp( g_tk, "ipaddr" ) == 0 ) {
                stat = nextToken( inFile );
                if ( stat & 1 ) {
                    strncpy( plc5IpAddr, g_tk, 31 );
                    plc5IpAddr[31] = 0;
                    findIpAddr = 0; /* got it */
                }
                else {
                    printf( "Missing plc ip address - abort\n" );
                    goto errExit;
                }
            }
            else {
                printf( "Expected \"ipaddr\", found \"%s\" - abort\n", g_tk );
                goto errExit;
            }

        }
    } while ( ( stat & 1 ) && findIpAddr );

    if ( !( stat & 1 ) ) {
        printf( "Unexpected end of file - abort\n" );
    }

    /* process modules */
    do {

        stat = nextToken( inFile );
        if ( stat & 1 ) {

            if ( strcmp( g_tk, "copy" ) == 0 ) {

                stat = nextToken( inFile );
                if ( stat & 1 ) {
                    if ( strcmp( g_tk, "{" ) != 0 ) {
                        printf( "Expected \"{\", found \"%s\" - abort\n", g_tk );
                        goto errExit;
                    }
                    printf( "stat = copyRegion( plc5IpAddr, inFile );\n" );
                    stat = copyRegion( plc5IpAddr, inFile );
                    if ( !( stat & 1 ) ) {
                        goto errExit;
                    } 
                }

            }
            else if ( strcmp( g_tk, "ofe" ) == 0 ) {

                stat = nextToken( inFile );
                if ( stat & 1 ) {
                    if ( strcmp( g_tk, "{" ) != 0 ) {
                        printf( "Expected \"{\", found \"%s\" - abort\n", g_tk );
                        goto errExit;
                    }
                    printf( "stat = configOfe( plc5IpAddr, inFile );\n" );
                    stat = configOfe( plc5IpAddr, inFile );
                    if ( !( stat & 1 ) ) {
                        goto errExit;
                    } 
                }

            }
            else if ( strcmp( g_tk, "niv" ) == 0 ) {

                stat = nextToken( inFile );
                if ( stat & 1 ) {
                    if ( strcmp( g_tk, "{" ) != 0 ) {
                        printf( "Expected \"{\", found \"%s\" - abort\n", g_tk );
                        goto errExit;
                    }
                    printf( "stat = configNiv( plc5IpAddr, inFile );\n" );
                    stat = configNiv( plc5IpAddr, inFile );
                    if ( !( stat & 1 ) ) {
                        goto errExit;
                    } 
                }

            }
            else if ( strcmp( g_tk, "ife" ) == 0 ) {

                stat = nextToken( inFile );
                if ( stat & 1 ) {
                    if ( strcmp( g_tk, "{" ) != 0 ) {
                        printf( "Expected \"{\", found \"%s\" - abort\n", g_tk );
                        goto errExit;
                    }
                    printf( "stat = configIfe( plc5IpAddr, inFile );\n" );
                    stat = configIfe( plc5IpAddr, inFile );
                    if ( !( stat & 1 ) ) {
                        goto errExit;
                    } 
                }

            }
            else if ( strcmp( g_tk, "nov" ) == 0 ) {

                stat = nextToken( inFile );
                if ( stat & 1 ) {
                    if ( strcmp( g_tk, "{" ) != 0 ) {
                        printf( "Expected \"{\", found \"%s\" - abort\n", g_tk );
                        goto errExit;
                    }
                    printf( "stat = configNov( plc5IpAddr, inFile );\n" );
                    stat = configNov( plc5IpAddr, inFile );
                    if ( !( stat & 1 ) ) {
                        goto errExit;
                    } 
                }

            }
            else if ( strcmp( g_tk, "nbv1" ) == 0 ) {

                stat = nextToken( inFile );
                if ( stat & 1 ) {
                    if ( strcmp( g_tk, "{" ) != 0 ) {
                        printf( "Expected \"{\", found \"%s\" - abort\n", g_tk );
                        goto errExit;
                    }
                    printf( "stat = configNbv1( plc5IpAddr, inFile );\n" );
                    stat = configNbv1( plc5IpAddr, inFile );
                    if ( !( stat & 1 ) ) {
                        goto errExit;
                    } 
                }

            }
            else if ( strcmp( g_tk, "nt1" ) == 0 ) {

                stat = nextToken( inFile );
                if ( stat & 1 ) {
                    if ( strcmp( g_tk, "{" ) != 0 ) {
                        printf( "Expected \"{\", found \"%s\" - abort\n", g_tk );
                        goto errExit;
                    }
                    printf( "stat = configNt1( plc5IpAddr, inFile );\n" );
                    stat = configNt1( plc5IpAddr, inFile );
                    if ( !( stat & 1 ) ) {
                        goto errExit;
                    } 
                }

            }

        }

    } while ( stat & 1 );

    fclose( inFile );

    return ERR_OK;

errExit:

    fclose( inFile );
    printf( "Execution aborted\n" );
    return ERR_FAIL;

}
