#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alarm.h"
#include "cvtTable.h"
#include "dbDefs.h"
#include "dbAccess.h"
#include "recGbl.h"
#include "recSup.h"
#include "devSup.h"
#include "link.h"
#include "callback.h"
#include "dbScan.h"
#include "epicsExport.h"
#include "menuConvert.h"

#include "epicsThread.h"
#include "epicsMutex.h"

#define ORNL_PLC5_SUCCESS 1
#define ORNL_PLC5_IGNORE 3

#define ORNL_PLC5_FAIL 2

#undef ORNL_DISA_WARM_START
#undef ORNL_RECORD_SUPPORT

#ifdef ORNL_RECORD_SUPPORT
#include	"ornlAiRecord.h"
#include	"ornlAoRecord.h"
#endif

#include	"aiRecord.h"
#include	"aoRecord.h"

#include	"biRecord.h"
#include	"boRecord.h"

#include "devOrnlPLC5-linux.h"

static long devOrnlPLC5Init();

#ifdef ORNL_RECORD_SUPPORT

static long devOrnlPLC5InitOrnlAiRecord();
static long devOrnlPLC5ReadOrnlAi();
static long devOrnlPLC5LinConvAi();

static long devOrnlPLC5InitOrnlAoRecord();
static long devOrnlPLC5WriteOrnlAo();
static long devOrnlPLC5LinConvAo();

#endif

static long devOrnlPLC5InitAiRecord();
static long devOrnlPLC5ReadAi();
static long devOrnlPLC5LinConvAi();

static long devOrnlPLC5InitAoRecord();
static long devOrnlPLC5WriteAo();
static long devOrnlPLC5LinConvAo();

static long devOrnlPLC5InitBiRecord();
static long devOrnlPLC5ReadBi();

static long devOrnlPLC5InitBoRecord();
static long devOrnlPLC5WriteBo();

/* standard EPICS declarations */

#ifdef ORNL_RECORD_SUPPORT

typedef struct {
  long number;
  DEVSUPFUN report;
  DEVSUPFUN init;
  DEVSUPFUN init_record;
  DEVSUPFUN get_ioint_info;
  DEVSUPFUN read_ai;
  DEVSUPFUN special_linconv;
} devOrnlPLC5OrnlAiType;

devOrnlPLC5OrnlAiType devOrnlPLC5OrnlAi = {
  6,
  NULL,
  devOrnlPLC5Init,
  devOrnlPLC5InitOrnlAiRecord,
  NULL,
  devOrnlPLC5ReadOrnlAi,
  devOrnlPLC5LinConvAi
};
epicsExportAddress(dset,devOrnlPLC5OrnlAi);

typedef struct {
  long number;
  DEVSUPFUN report;
  DEVSUPFUN init;
  DEVSUPFUN init_record;
  DEVSUPFUN get_ioint_info;
  DEVSUPFUN write_ao;
  DEVSUPFUN special_linconv;
} devOrnlPLC5OrnlAoType;

devOrnlPLC5OrnlAoType devOrnlPLC5OrnlAo = {
  6,
  NULL,
  devOrnlPLC5Init,
  devOrnlPLC5InitOrnlAoRecord,
  NULL,
  devOrnlPLC5WriteOrnlAo,
  devOrnlPLC5LinConvAo
};
epicsExportAddress(dset,devOrnlPLC5OrnlAo);

#endif

typedef struct {
  long number;
  DEVSUPFUN report;
  DEVSUPFUN init;
  DEVSUPFUN init_record;
  DEVSUPFUN get_ioint_info;
  DEVSUPFUN read_ai;
  DEVSUPFUN special_linconv;
} devOrnlPLC5AiType;

devOrnlPLC5AiType devOrnlPLC5Ai = {
  6,
  NULL,
  devOrnlPLC5Init,
  devOrnlPLC5InitAiRecord,
  NULL,
  devOrnlPLC5ReadAi,
  devOrnlPLC5LinConvAi
};
epicsExportAddress(dset,devOrnlPLC5Ai);

typedef struct {
  long number;
  DEVSUPFUN report;
  DEVSUPFUN init;
  DEVSUPFUN init_record;
  DEVSUPFUN get_ioint_info;
  DEVSUPFUN write_ao;
  DEVSUPFUN special_linconv;
} devOrnlPLC5AoType;

devOrnlPLC5AoType devOrnlPLC5Ao = {
  6,
  NULL,
  devOrnlPLC5Init,
  devOrnlPLC5InitAoRecord,
  NULL,
  devOrnlPLC5WriteAo,
  devOrnlPLC5LinConvAo
};
epicsExportAddress(dset,devOrnlPLC5Ao);

typedef struct {
  long number;
  DEVSUPFUN report;
  DEVSUPFUN init;
  DEVSUPFUN init_record;
  DEVSUPFUN get_ioint_info;
  DEVSUPFUN write_bo;
} devOrnlPLC5BoType;

devOrnlPLC5BoType devOrnlPLC5Bo = {
  5,
  NULL,
  devOrnlPLC5Init,
  devOrnlPLC5InitBoRecord,
  NULL,
  devOrnlPLC5WriteBo,
};
epicsExportAddress(dset,devOrnlPLC5Bo);

typedef struct {
  long number;
  DEVSUPFUN report;
  DEVSUPFUN init;
  DEVSUPFUN init_record;
  DEVSUPFUN get_ioint_info;
  DEVSUPFUN read_bi;
} devOrnlPLC5BiType;

devOrnlPLC5BiType devOrnlPLC5Bi = {
  5,
  NULL,
  devOrnlPLC5Init,
  devOrnlPLC5InitBiRecord,
  NULL,
  devOrnlPLC5ReadBi,
};
epicsExportAddress(dset,devOrnlPLC5Bi);

typedef long (*checkFunc)( dbCommon *genPtr );

typedef struct callBackTag {
  struct callBackTag *flink;
  checkFunc cf;
  dbCommon *rec;
} callBackType, *callBackPtr;

static epicsMutexId scannerLock;
static int scannerStarted = 0;
static int scanReady = 0;
static callBackPtr callBackHead = NULL;
static callBackPtr callBackTail = NULL;

void notifyPLC5 ( void ) {

  epicsMutexLock( scannerLock );
  scanReady = 1;
  epicsMutexUnlock( scannerLock );

}

/**
 * This is the comparison scanner that checks to see if a PLC location
 * has changed. It triggers the Check callback for an output record and reads the PLC location.
 * It processes the record if the result of the callback is SUCCESS
 *
 * For Direct records, the result will be IGNORE, so the record is not processed
 */
static void scanner (
  void *arg
) {

long stat;
int process, waiting;
epicsEnum16 scanType;
callBackPtr curCallBack;

  waiting = 1;
  do {

    epicsMutexLock( scannerLock );
    if ( scanReady ) {
      waiting = 0;
    }
    epicsMutexUnlock( scannerLock );

    epicsThreadSleep( 1.0 );

  } while ( waiting );

  while ( 1 ) {

    curCallBack = callBackHead->flink;
    while ( curCallBack ) {

      process = 0;
      dbScanLock( curCallBack->rec );

      scanType = curCallBack->rec->scan;

      if ( !(curCallBack->rec->pact) ) {
        stat = (curCallBack->cf)( curCallBack->rec );
				if ( stat == ORNL_PLC5_SUCCESS ) {
					process = 1;
				}
      }

      dbScanUnlock( curCallBack->rec );

      if ( process ) {
/*	 printf( "call scanOnce \n");  */
        scanOnce( curCallBack->rec );
      }

      curCallBack = curCallBack->flink;

    }

    epicsThreadSleep( 1.0 );

  }

}

/**
 * Start the scanner thread for the Check callbacks
 */
static void startScanner( void ) {

epicsThreadId id;

 if ( scannerStarted ) return;

  scannerStarted = 1;

  id = epicsThreadCreate( "plc5_scanner",
   epicsThreadPriorityLow,
   epicsThreadGetStackSize( epicsThreadStackMedium ),
   scanner, NULL );

}

static int parseParams (
  char *string,
  char *devName,
  int *unit,
  char *moduleName,
  int *channel,
  char *plc5Addr
) {

char *buf, *tok, tmp[63+1];
int needChannel;

  /* defaults for optional parameters */
  *unit = -1;

  strcpy( tmp, string );

  buf = NULL;
  tok = strtok_r( tmp, " \t\n", &buf );
  if ( tok ) {
    strncpy( devName, tok, 31 );
    devName[31] = 0;
  }
  else {
    goto err_return;
  }

  tok = strtok_r( NULL, " \t\n", &buf );
  if ( tok ) {
    *unit = atol( tok );
  }
  else {
    goto err_return;
  }

  tok = strtok_r( NULL, " \t\n", &buf );
  if ( tok ) {
    strncpy( moduleName, tok, 31 );
    moduleName[31] = 0;
  }
  else {
    goto err_return;
  }

  if ( strcmp( moduleName, "ofeBin" ) == 0 ) {
    needChannel = 1;
  }
  else if ( strcmp( moduleName, "nov" ) == 0 ) {
    needChannel = 1;
  }
  else if ( strcmp( moduleName, "ifeBin" ) == 0 ) {
    needChannel = 1;
  }
  else if ( strcmp( moduleName, "nt1" ) == 0 ) {
    needChannel = 1;
  }
  else if ( strcmp( moduleName, "niv" ) == 0 ) {
    needChannel = 1;
  }
  else if ( strcmp( moduleName, "nbv1in" ) == 0 ) {
    needChannel = 1;
  }
  else if ( strcmp( moduleName, "nbv1out" ) == 0 ) {
    needChannel = 1;
  }
  else if ( strcmp( moduleName, "discrete" ) == 0 ) {
    needChannel = 0;
  }
  else if ( strcmp( moduleName, "word" ) == 0 ) {
    needChannel = 0;
  }
  else if ( strcmp( moduleName, "status" ) == 0 ) {
    needChannel = 0;
  }
  else if ( strcmp( moduleName, "bit" ) == 0 ) {
    needChannel = 0;
  }
  else if ( strcmp( moduleName, "bitd" ) == 0 ) {
    needChannel = 0;
  }
  else if ( strcmp( moduleName, "wordd" ) == 0 ) {
    needChannel = 0;
  }
  else {
    printf( "Unknown module (%s) or I/O type in %s at line %-d\n",
     moduleName, __FILE__, __LINE__ );
    goto err_return;
  }

  if ( needChannel ) {

    tok = strtok_r( NULL, " \t\n", &buf );
    if ( tok ) {
      *channel = atol( tok );
    }
    else {
      goto err_return;
    }

  }
  else {

    *channel = 0;

  }

  tok = strtok_r( NULL, " \t\n", &buf );
  if ( tok ) {
    strncpy( plc5Addr, tok, 31 );
    plc5Addr[31] = 0;
  }
  else {
    goto err_return;
  }

  return 1;

err_return:
  strcpy( devName, "" );
  *unit = -1;
  strcpy( moduleName, "" );
  *channel = 0;
  strcpy( plc5Addr, "" );
  return 0;

}

/**
 * Parse the optional check scan block number on the end of the regular parameters
 * string. If the parameter is not there or invalid, we will assign a value of -1
 * which will result in this check being ignored.
 *
 * For example, >>   @tr13ioc 3 bitd N13:27/1 4   << describes a direct access bit that has its check
 * bit in scan block 4. We will look in that scan block for the value instead of reading the PLC
 * directly to reduce network traffic
 */
static int parseParamsCheck (
  char *string,
  int *unitCheck
) {

  char *tok;

  *unitCheck = -1;

/**
 * Read the unitCheck for any direct word or bit address. Start from the end of the string,
 * work back to the first space, and convert to int.
 */
  tok = strrchr(string,' ');
  if ( tok ) {
    tok++;
		if (tok) {
			if (isalpha(*tok)) {
				*unitCheck = -1;
			} else {
			  *unitCheck = atol( tok );
			}
  		return 1;
		}
  }
  return 0;
}

static long devOrnlPLC5Init (
  int afterDBinit
) {

/*int count;*/
static int firstBefore=1, firstAfter=1;

  if ( !afterDBinit && firstBefore ) {

    callBackHead = (callBackPtr) calloc( 1, sizeof(callBackType) );
    if ( !callBackHead ) {
      printf( "devOrnlPLC5Init - mem alloc failed\n" );
    }
    callBackTail = callBackHead;
    callBackTail->flink = NULL;

    scannerLock  = epicsMutexCreate();

    firstBefore = 0;

  }

  if ( afterDBinit && firstAfter ) {

#if 1
    startScanner();
#else
    printf("\ndevOrnlPLC5 Ao/Bo Scanner disabled\n");
#endif

    firstAfter = 0;

  }

  return 0;

}

#ifdef ORNL_RECORD_SUPPORT

/*************************************************************************/
/*                                                                       */
/*                                   ornlAi                              */
/*                                                                       */
/*************************************************************************/

static long devOrnlPLC5InitOrnlAiRecord (
  struct ornlAiRecord *ptr
) {

ornlPLC5DevHandle handle;
char moduleName[31+1], devName[31+1], plc5Addr[31+1];
int unit, channel, stat;
struct instio *pinstio;

  pinstio = (struct instio *) &ptr->inp.value;
  ptr->dpvt = (void *) NULL;

  /* parse device params */
  stat = parseParams( pinstio->string, devName, &unit, moduleName, &channel,
   plc5Addr );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "devOrnlPLC5InitOrnlAiRecord, error %d - parseParams - %s\n",
     stat, devName );
    goto err_return;
  }

  stat = ornlPLC5GetDevHandle( devName, unit, moduleName, channel,
   plc5Addr, &handle );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "devOrnlPLC5InitOrnlAiRecord, error %d - ornlPLC5GetDevHandle - %s\n",
     stat, devName );
    goto err_return;
  }

  ptr->dpvt = (void *) handle;

  return 0;

err_return:
  ptr->dpvt = (void *) NULL;
  return 0;

}

static long devOrnlPLC5ReadOrnlAi (
  struct ornlAiRecord *ptr
) {

ornlPLC5DevHandle handle = (ornlPLC5DevHandle) ptr->dpvt;
int stat, value;

  if ( ptr->pact ) return 0;

  stat = ornlPLC5ReadWord( handle, &value );
  if ( stat & 1 ) {
    ptr->udf = FALSE; /* this needs to be done explicitly */
    ptr->rval = value;
  }
  else {
    ptr->udf = TRUE;
    recGblSetSevr( ptr, READ_ALARM, INVALID_ALARM);
  }

  return 0;

}

/*************************************************************************/
/*                                                                       */
/*                                   ornlAo                              */
/*                                                                       */
/*************************************************************************/

static long devOrnlPLC5CheckOrnlAo (
  dbCommon *genPtr
) {

int stat, force, rval1, rval2;
double dbl;
struct ornlAoRecord *ptr = (ornlAoRecord *) genPtr;
ornlPLC5DevHandle handle;

  if ( ptr ) {

    handle = (ornlPLC5DevHandle) ptr->dpvt;

    if ( handle ) {

      stat = ornlPLC5ReadWord( handle, &rval1 );
      if ( !( stat & 1 ) ) {
        recGblSetSevr( genPtr, WRITE_ALARM, INVALID_ALARM);
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
	}
        dbScanUnlock( genPtr );
        return ORNL_PLC5_FAIL;
      }

      stat = ornlPLC5ReadOtherWord( handle, &rval2 );
      if ( !( stat & 1 ) ) {
        recGblSetSevr( genPtr, WRITE_ALARM, INVALID_ALARM);
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
	}
        dbScanUnlock( genPtr );
        return ORNL_PLC5_FAIL;
      }

      /* printf( "devOrnlPLC5CheckOrnlAo - rval1 = %-d, rval2 = %-d\n",
	 rval1, rval2 ); */

      force = ornlPLC5CheckForce( handle );
      /* printf( "devOrnlPLC5CheckOrnlAo - force = %-d\n", force ); */

      if ( force ) {
        ptr->udf = FALSE;
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
	}
        dbScanUnlock( genPtr );
      }

      if ( !force && ( rval1 == rval2 ) ) {
        return ORNL_PLC5_IGNORE;
      }

      ptr->rval = rval2;
      dbl = rval2 * ptr->aslo + ptr->aoff;

      switch (ptr->linr) {
      case menuConvertNO_CONVERSION:
        ptr->val = ptr->pval = dbl;
        ptr->udf = FALSE;
        break;
      case menuConvertLINEAR:
        dbl = dbl*ptr->eslo + ptr->eoff;
        ptr->val = ptr->pval = dbl;
        ptr->udf = FALSE;
        break;
      default:
        if ( cvtRawToEngBpt( &dbl, ptr->linr, ptr->init,
         (void *)&ptr->pbrk, &ptr->lbrk ) != 0 ) {
          break; /* cannot back-convert */
	}
        ptr->val = ptr->pval = dbl;
        ptr->udf = FALSE;
      }

      /* printf( "devOrnlPLC5CheckOrnlAo - rval = %-d, val = %-f\n",
	 ptr->rval, ptr->val ); */

      return ORNL_PLC5_SUCCESS;

    }

  }

  return ORNL_PLC5_FAIL;

}

static int ornlAoReadIniVal (
  ornlAoRecord *ptr
) {

int stat, rval;
ornlPLC5DevHandle handle = (ornlPLC5DevHandle) ptr->dpvt;

  stat = ornlPLC5ReadWord( handle, &rval );
  if ( !( stat & 1 ) ) {
    recGblSetSevr( ptr, WRITE_ALARM, INVALID_ALARM);
    if ( plc5Msgs() ) printf( "ornlAoReadIniVal - ornlPLC5ReadWord failed\n" );
    return ORNL_PLC5_FAIL;
  }

  ptr->rval = rval;

  return ORNL_PLC5_SUCCESS;

}

static long devOrnlPLC5InitOrnlAoRecord (
  struct ornlAoRecord *ptr
) {

ornlPLC5DevHandle handle;
char moduleName[31+1], devName[31+1], plc5Addr[31+1];
int unit, channel, stat;
struct instio *pinstio;
callBackPtr curCallBack;

  pinstio = (struct instio *) &ptr->out.value;
  ptr->dpvt = (void *) NULL;

  /* parse device params */
  stat = parseParams( pinstio->string, devName, &unit, moduleName, &channel,
   plc5Addr );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "devOrnlPLC5InitOrnlAoRecord, error %d - parseParams - %s\n",
     stat, devName );
    goto err_return;
  }

  stat = ornlPLC5GetDevHandle( devName, unit, moduleName, channel,
   plc5Addr, &handle );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "devOrnlPLC5InitOrnlAoRecord, error %d - ornlPLC5GetDevHandle\n",
     stat );
    goto err_return;
  }

  ptr->dpvt = (void *) handle;

  curCallBack = (callBackPtr) calloc( 1, sizeof(callBackType) );
  if ( curCallBack ) {
    curCallBack->cf = devOrnlPLC5CheckOrnlAo;
    curCallBack->rec = (dbCommon *) ptr;
    callBackTail->flink = curCallBack;
    callBackTail = curCallBack;
    callBackTail->flink = NULL;
  }

  stat = ornlAoReadIniVal( ptr );
  if ( stat & 1 ) return 0; /* calc ini val from rval */

  return 2; /* don't calc ini val from rval */

err_return:
  ptr->dpvt = (void *) NULL;
  return -1;

}

static long devOrnlPLC5WriteOrnlAo (
  struct ornlAoRecord *ptr
) {

int stat;
ornlPLC5DevHandle handle = (ornlPLC5DevHandle) ptr->dpvt;

  if ( ptr->pact ) return 0;

#ifdef ORNL_DISA_WARM_START
  if ( ptr->disa == 0 ) {
    return 2; /* don't convert */
  }
#endif

  stat = ornlPLC5WriteWord( handle, ptr->rval );
  if ( !( stat & 1 ) ) {
    recGblSetSevr( ptr, WRITE_ALARM, INVALID_ALARM);
  }

  return 0;

}

#endif

/*************************************************************************/
/*                                                                       */
/*                                   ai                                  */
/*                                                                       */
/*************************************************************************/

static long devOrnlPLC5InitAiRecord (
  struct aiRecord *ptr
) {

ornlPLC5DevHandle handle;
char moduleName[31+1], devName[31+1], plc5Addr[31+1];
int unit, channel, stat;
struct instio *pinstio;

  pinstio = (struct instio *) &ptr->inp.value;
  ptr->dpvt = (void *) NULL;

#if 0
  printf( "devOrnlPLC5InitAiRecord - parseParams %s\n", pinstio->string);
#endif

  /* parse device params */
  stat = parseParams( pinstio->string, devName, &unit, moduleName, &channel,
   plc5Addr );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) {
      printf( "devOrnlPLC5InitAiRecord - %s - error %d - parseParams - %s\n",
        ptr->name, stat, pinstio->string );
    }
    goto err_return;
  }

#if 0
  printf( "devName %s unit %d moduleName %s channel %d\n", devName, unit, moduleName, channel);
#endif

  stat = ornlPLC5GetDevHandle( devName, unit, moduleName, channel,
   plc5Addr, &handle );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) {
      printf( "devOrnlPLC5InitAiRecord, error %d - ornlPLC5GetDevHandle\n", stat );
    }
    goto err_return;
  }

  ptr->dpvt = (void *) handle;
  devOrnlPLC5LinConvAi(ptr, TRUE);
  return 0;

err_return:
  ptr->dpvt = (void *) NULL;
  return 0;

}

static long devOrnlPLC5ReadAi (
  struct aiRecord *ptr
) {

ornlPLC5DevHandle handle = (ornlPLC5DevHandle) ptr->dpvt;
int stat, value;

  if ( ptr->pact ) return 0;

  stat = ornlPLC5ReadWord( handle, &value );
  if ( stat & 1 ) {
    ptr->udf = FALSE; /* this needs to be done explicitly */
    ptr->rval = value;
  }
  else {
    ptr->udf = TRUE;
    recGblSetSevr( ptr, READ_ALARM, INVALID_ALARM);
  }

  return 0;

}

/******************************************************************************
*
* LinConvAi - The special_linconv device support routine
* 
* Sets the engineering units slope to be the range of the signal in
* engineering units divided by 2048
* 
* **** NOTE: For all device types data are now left-shifted to 16 bits
*            for historical compatibility we expect EGUL to be the 
*            engineering unit value at 0 ADC voltage
*
* RETURNS:
* 0 - no errors.
*/

LOCAL long devOrnlPLC5LinConvAi
    (
     struct aiRecord *pRec,   /* Analog input record to use */
     int after                /* True if after record processing */
     )
{
    /* set linear conversion slope*/
    if( after ) {
        pRec->eslo = (pRec->eguf - pRec->egul)/4095.0;
/*	printf("LinConvAi %s %f %f %f\n", pRec->name, pRec->eguf, pRec->egul, pRec->eslo); */
    }	
    return(0);
}



/*************************************************************************/
/*                                                                       */
/*                                   ao                                  */
/*                                                                       */
/*************************************************************************/

static long devOrnlPLC5CheckAo (
  dbCommon *genPtr
) {

int stat, force, rval1, rval2;
double dbl;
struct aoRecord *ptr = (aoRecord *) genPtr;
ornlPLC5DevHandle handle;

  if ( ptr ) {

    handle = (ornlPLC5DevHandle) ptr->dpvt;

    if ( handle ) {

      stat = ornlPLC5ReadWord( handle, &rval1 );
      if ( !( stat & 1 ) ) {
        recGblSetSevr( genPtr, WRITE_ALARM, INVALID_ALARM);
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
	      }
        dbScanUnlock( genPtr );
        return ORNL_PLC5_FAIL;
      }

      stat = ornlPLC5ReadOtherWord( handle, &rval2 );
      if ( !( stat & 1 ) ) {
        recGblSetSevr( genPtr, WRITE_ALARM, INVALID_ALARM);
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
				}
        dbScanUnlock( genPtr );
        return ORNL_PLC5_FAIL;
      }

      printf( "devOrnlPLC5CheckAo - addr %s rval1 = %-d, rval2 = %-d\n",
				((ornlPLC5DevPtr) handle)->plc5Addr, rval1, rval2 );

      force = ornlPLC5CheckForce( handle );
/*       printf( "devOrnlPLC5CheckAo - force = %-d\n", force );  */

      if ( force ) {
		
/*		printf("Check force A0\n"); */
		
        ptr->udf = FALSE;
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
	}
        dbScanUnlock( genPtr );
      }

      if ( !force && ( rval1 == rval2 ) ) {

/*		printf("Check force A0 ignore\n"); */
        return ORNL_PLC5_IGNORE;
      }

      ptr->rval = rval2;
      dbl = rval2 * ptr->aslo + ptr->aoff;

      switch (ptr->linr) {
      case menuConvertNO_CONVERSION:
        ptr->val = ptr->pval = dbl;
        ptr->udf = FALSE;
        break;
      case menuConvertLINEAR:
        dbl = dbl*ptr->eslo + ptr->eoff;
        ptr->val = ptr->pval = dbl;
        ptr->udf = FALSE;
        break;
      default:
        if ( cvtRawToEngBpt( &dbl, ptr->linr, ptr->init,
         (void *)&ptr->pbrk, &ptr->lbrk ) != 0 ) {
          break; /* cannot back-convert */
	}
        ptr->val = ptr->pval = dbl;
        ptr->udf = FALSE;
      }

      /* printf( "devOrnlPLC5CheckAo - rval = %-d, val = %-f\n",
	 ptr->rval, ptr->val ); */

      return ORNL_PLC5_SUCCESS;

    }

  }

  return ORNL_PLC5_FAIL;

}

/**
 * This function is called by the scanner to see if the PLC data has changed compared to the PV.
 * It reads the scan block data (handleCheck) and then updates the PV value. No processing is
 * done with these checks
 */
static long devOrnlPLC5CheckAoDirect (
  dbCommon *genPtr
) {

int stat, force, rval1;
double dbl;
struct aoRecord *ptr = (aoRecord *) genPtr;
ornlPLC5DevHandle handle;
ornlPLC5DevHandle handleCheck;

  if ( ptr ) {

    handle = (ornlPLC5DevHandle) ptr->dpvt;
    handleCheck = (ornlPLC5DevHandle) ((ornlPLC5DevPtr) ptr->dpvt)->check;

    if ( handleCheck ) {

      stat = ornlPLC5ReadWord( handleCheck, &rval1 );
      if ( !( stat & 1 ) ) {
        recGblSetSevr( genPtr, WRITE_ALARM, INVALID_ALARM);
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
	      }
        dbScanUnlock( genPtr );
        return ORNL_PLC5_FAIL;
      }

/*      printf( "devOrnlPLC5CheckAoDirect - rval1 = %-d, rval = %-d\n",
	 rval1, ptr->rval ); */

      force = ornlPLC5CheckForce( handle );
/*       printf( "devOrnlPLC5CheckAoDirect - force = %-d\n", force );  */

      if ( force ) {
        ptr->udf = FALSE;
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
	}
        dbScanUnlock( genPtr );
      }

      if ( !force && ( rval1 == ptr->rval ) ) {
        return ORNL_PLC5_IGNORE;
      }

#if 0
      printf( "devOrnlPLC5CheckAoDirect %s - new = %d, last = %d\n",	 
				((aoRecord *) ptr)->name,  ptr->rval, rval1 );
#endif
      ptr->rval = rval1;
/*      dbl = rval1 * ptr->aslo + ptr->aoff; */
      dbl = rval1;

      switch (ptr->linr) {
      case menuConvertNO_CONVERSION:
        ptr->val = ptr->pval = dbl;
        ptr->udf = FALSE;
        break;
      case menuConvertLINEAR:
        dbl = dbl*ptr->eslo + ptr->eoff;
        ptr->val = ptr->pval = dbl;
        ptr->udf = FALSE;
        break;
      default:
        if ( cvtRawToEngBpt( &dbl, ptr->linr, ptr->init,
         (void *)&ptr->pbrk, &ptr->lbrk ) != 0 ) {
          break; /* cannot back-convert */
			}
			
        ptr->val = ptr->pval = dbl;
        ptr->udf = FALSE;
		  
      }
      ((ornlPLC5DevPtr) ptr->dpvt)->checkFlag = 1;
		  
      return ORNL_PLC5_SUCCESS;

    }
    return ORNL_PLC5_IGNORE;
  }
  return ORNL_PLC5_FAIL;
}

static int aoReadIniVal (
  struct aoRecord *ptr
) {

int stat, rval;
ornlPLC5DevHandle handle = (ornlPLC5DevHandle) ptr->dpvt;

  stat = ornlPLC5ReadWord( handle, &rval );
  if ( !( stat & 1 ) ) {
    recGblSetSevr( ptr, WRITE_ALARM, INVALID_ALARM);
    if ( plc5Msgs() ) printf( "aoReadIniVal - ornlPLC5ReadWord failed\n" );
    return ORNL_PLC5_FAIL;
  }

  ptr->rval = rval;

#if 0
  printf( "aoReadIniVal - %s rval %d\n", ((ornlPLC5DevPtr) handle)->plc5Addr, rval);
#endif

  return ORNL_PLC5_SUCCESS;

}

/**
 * The Init function parses out the PLC address and gets a device handle from driver support. For direct
 * type devices, the tail end of the OUTP field is checked to see if there is a scan block number following
 * the PLC address. If missing or -1, then the device is not placed on the Check queue. If present, then
 * the device gets another handle which is for a bit input from the scan block identified by
 * the extra scan block number. This second handle is attached to the ->spvt address in the 
 * ao data structure.
 */
static long devOrnlPLC5InitAoRecord (
  struct aoRecord *ptr
) {

ornlPLC5DevHandle handle;
ornlPLC5DevHandle handleCheck;
char moduleName[31+1], devName[31+1], plc5Addr[31+1], plc5Trans[31+1];
int unit, channel, stat, unitCheck;
struct instio *pinstio;
callBackPtr curCallBack;

  pinstio = (struct instio *) &ptr->out.value;
  ptr->dpvt = (void *) NULL;

#if 0
  printf( "devOrnlPLC5InitAoRecord - parseParams %s\n", pinstio->string);
#endif


  /* parse device params */
  stat = parseParams( pinstio->string, devName, &unit, moduleName, &channel,
   plc5Addr );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "devOrnlPLC5InitAoRecord - %s - error %d - parseParams - %s\n",
     ptr->name, stat, devName );
    goto err_return;
  }

  if (0 == strcmp(moduleName, "wordd")) {
    /* parse device params */
    stat = parseParamsCheck( pinstio->string, &unitCheck );
    if ( !( stat & 1 ) ) {
      if ( plc5Msgs() ) printf( "devOrnlPLC5InitAoRecord - %s - error %d - parseParams - %s\n",
       ptr->name, stat, devName );
      goto err_return;
    }
/*printf("InitAoRecord %s %d\n", pinstio->string, unitCheck);*/
  }

  /* Convert octal to decimal for I: and O: */
  strcpy(plc5Trans, plc5Addr);
  transAddr(plc5Trans);
  
  stat = ornlPLC5GetDevHandle( devName, unit, moduleName, channel, plc5Trans, &handle );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "devOrnlPLC5InitAoRecord, error %d - ornlPLC5GetDevHandle\n",
     stat );
    goto err_return;
  }

  ptr->dpvt = (void *) handle;

/*	Where we have a direct ao record, and a check scan block specified
	get a second handle for the unitCheck scan block but as a word input.
	This will be used to compare the ao value with the check value to detect
	if the PLC has changed a ao word.
*/	
  if ((0 == strcmp(moduleName, "wordd")) && (unitCheck > -1)) {
  	stat = ornlPLC5GetDevHandle( devName, unitCheck, "word", channel, plc5Addr, &handleCheck );
  	if ( !( stat & 1 ) ) {
    	if ( plc5Msgs() ) printf( "devOrnlPLC5InitAoRecord, error %d - ornlPLC5GetDevHandle - %s\n",
    	 stat, devName);
    	goto err_return;
  	}
#if 1
	  ((ornlPLC5DevPtr) ptr->dpvt)->check = (ornlPLC5DevHandle) handleCheck;
	  /* if the PV specifies PINI as YES, then set the checkFlag so that on
	     processing at init, it will not write to the output
	*/
	((ornlPLC5DevPtr) ptr->dpvt)->checkFlag = (ptr->pini ? 1 : 0);
#endif		
	} else {
#if 1
	  ((ornlPLC5DevPtr) ptr->dpvt)->check = NULL;
#endif
	}

/**
 * Add the record into the scan list for output checking if unitCheck is greater than -1
 */
  if (unitCheck > -1) {
	 curCallBack = (callBackPtr) calloc( 1, sizeof(callBackType) );
	 if ( curCallBack ) {
		  if (strcmp( moduleName, "wordd" ) == 0 ) {
	   	curCallBack->cf = devOrnlPLC5CheckAoDirect;
		  } else {
	   	curCallBack->cf = devOrnlPLC5CheckAo;
		  }
   	curCallBack->rec = (dbCommon *) ptr;
   	callBackTail->flink = curCallBack;
   	callBackTail = curCallBack;
   	callBackTail->flink = NULL;
	 }
  } else {
/*    printf("Skipped adding %s into ao scan list\n", plc5Addr); */
  }
  
  devOrnlPLC5LinConvAo(ptr, TRUE);

  stat = aoReadIniVal( ptr );
  if ( stat & 1 ) return 0; /* calc ini val from rval */
  return 2; /* don't calc ini val from rval */

err_return:
  ptr->dpvt = (void *) NULL;
  return -1;

}
static long devOrnlPLC5WriteAo (
  struct aoRecord *ptr
) {

int stat;
ornlPLC5DevHandle handle = (ornlPLC5DevHandle) ptr->dpvt;

#if 0
  printf( "devOrnlPLC5WriteAo\n");
#endif

  if ( ptr->pact ) {
#if 0
  printf( "return pact TRUE\n");
#endif
    return 0;
}

/* if the check flag is set we don't do a write, but the record will still process
   The check flag is set in the check scan routine for any changed Ao PV */
	
  if (((ornlPLC5DevPtr) ptr->dpvt)->checkFlag != 0) {
    ((ornlPLC5DevPtr) ptr->dpvt)->checkFlag = 0;
	 return 0;
  }

#ifdef ORNL_DISA_WARM_START
  if ( ptr->disa == 0 ) {
    return 2; /* don't convert */
  }
#endif

  stat = ornlPLC5WriteWord( handle, ptr->rval );
  if ( !( stat & 1 ) ) {
    recGblSetSevr( ptr, WRITE_ALARM, INVALID_ALARM);
  }

  return 0;

}

/******************************************************************************
*
* LinConvAo - The special_linconv device support routine
* 
* Sets the engineering units slope to be the range of the signal in
* engineering units divided by 2048
* 
* **** NOTE: For all device types data are now left-shifted to 16 bits
*            for historical compatibility we expect EGUL to be the 
*            engineering unit value at 0 ADC voltage
*
* RETURNS:
* 0 - no errors.
*/

LOCAL long devOrnlPLC5LinConvAo
    (
     struct aoRecord *pRec,   /* Analog input record to use */
     int after                /* True if after record processing */
     )
{
    /* set linear conversion slope*/
    if( after ) {
    	pRec->eslo = (pRec->eguf - pRec->egul)/4095.0;
/*	printf("LinConvAo %s %f %f %f\n", pRec->name, pRec->eguf, pRec->egul, pRec->eslo); */
    }
    return(0);
}



/*************************************************************************/
/*                                                                       */
/*                                   bi                                  */
/*                                                                       */
/*************************************************************************/

static long devOrnlPLC5InitBiRecord (
  struct biRecord *ptr
) {

ornlPLC5DevHandle handle;
char moduleName[31+1], devName[31+1], plc5Addr[31+1];
int unit, channel, stat;
struct instio *pinstio;

  pinstio = (struct instio *) &ptr->inp.value;
  ptr->dpvt = (void *) NULL;

#if 0
  printf( "devOrnlPLC5InitBiRecord - parseParams %s\n", pinstio->string);
#endif

  /* parse device params */
  stat = parseParams( pinstio->string, devName, &unit, moduleName, &channel,
   plc5Addr );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "devOrnlPLC5InitBiRecord - %s - error %d - parseParams - %s\n",
     ptr->name, stat, devName );
    goto err_return;
  }

#if 0
if (strstr(pinstio->string, "O:074")) {
  printf("InitBiRecord %s %s %d %s %d %s\n", 
    pinstio->string, devName, unit, moduleName, channel, plc5Addr);
}
#endif

  stat = ornlPLC5GetDevHandle( devName, unit, moduleName, channel,
   plc5Addr, &handle );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "devOrnlPLC5InitBiRecord, error %d - ornlPLC5GetDevHandle\n",
     stat );
    goto err_return;
  }

  ptr->dpvt = (void *) handle;

  return 0;

err_return:
  ptr->dpvt = (void *) NULL;
  return 0;

}

static long devOrnlPLC5ReadBi (
  struct biRecord *ptr
) {

ornlPLC5DevHandle handle = (ornlPLC5DevHandle) ptr->dpvt;
int stat, value;

  if ( ptr->pact ) return 0;

  stat = ornlPLC5ReadBit( handle, &value );
  if ( stat & 1 ) {
    ptr->udf = FALSE; /* this needs to be done explicitly */
    ptr->rval = (short) value;
  }
  else {
    ptr->udf = TRUE;
    recGblSetSevr( ptr, READ_ALARM, INVALID_ALARM);
  }

  return 0;

}

/*************************************************************************/
/*                                                                       */
/*                                   bo                                  */
/*                                                                       */
/*************************************************************************/

static long devOrnlPLC5CheckBo (
  dbCommon *genPtr
) {

int stat, force, rval1, rval2;
struct boRecord *ptr = (boRecord *) genPtr;
ornlPLC5DevHandle handle;

  if ( ptr ) {

    handle = (ornlPLC5DevHandle) ptr->dpvt;

    if ( handle ) {

      stat = ornlPLC5ReadBit( handle, &rval1 );
      if ( !( stat & 1 ) ) {
        recGblSetSevr( genPtr, WRITE_ALARM, INVALID_ALARM);
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
	}
        dbScanUnlock( genPtr );
        return ORNL_PLC5_FAIL;
      }

      stat = ornlPLC5ReadOtherBit( handle, &rval2 );
      if ( !( stat & 1 ) ) {
        recGblSetSevr( genPtr, WRITE_ALARM, INVALID_ALARM);
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
	}
        dbScanUnlock( genPtr );
        return ORNL_PLC5_FAIL;
      }

      /* printf( "devOrnlPLC5CheckBo - rval1 = %-d, rval2 = %-d\n",
	 rval1, rval2 ); */

      force = ornlPLC5CheckForce( handle );
      /* printf( "devOrnlPLC5CheckBo - force = %-d\n", force ); */

      if ( force ) {
        ptr->udf = FALSE;
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
	}
        dbScanUnlock( genPtr );
      }

      if ( !force && ( rval1 == rval2 ) ) {
        return ORNL_PLC5_IGNORE;
      }

      ptr->rval = rval2;
      ptr->val = rval2;

      /* printf( "devOrnlPLC5CheckBo - rval = %-d, val = %-d\n",
	 ptr->rval, (int) ptr->val ); */

      return ORNL_PLC5_SUCCESS;

    }

  }

  return ORNL_PLC5_FAIL;

}

/**
 * This function is called by the scanner to see if the PLC data has changed compared to the PV.
 * It reads the scan block data (handleCheck) and then updates the PV value. No processing is
 * done with these checks
 */
static long devOrnlPLC5CheckBoDirect (
  dbCommon *genPtr
) {

int stat, force, rval1;
struct boRecord *ptr = (boRecord *) genPtr;
ornlPLC5DevHandle handle;
ornlPLC5DevHandle handleCheck;

  if ( ptr ) {

    handle = (ornlPLC5DevHandle) ptr->dpvt;
    handleCheck = (ornlPLC5DevHandle) ((ornlPLC5DevPtr) ptr->dpvt)->check;

    if ( handleCheck ) {

      stat = ornlPLC5ReadBit( handleCheck, &rval1 );
      if ( !( stat & 1 ) ) {
        recGblSetSevr( genPtr, WRITE_ALARM, INVALID_ALARM);
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
				}
        dbScanUnlock( genPtr );
        return ORNL_PLC5_FAIL;
      }

      force = ornlPLC5CheckForce( handle );
      /* printf( "devOrnlPLC5CheckBo - force = %-d\n", force ); */

      if ( force ) {
        ptr->udf = FALSE;
        dbScanLock( genPtr );
        if ( !genPtr->pact ) {
          struct rset *rset= (struct rset *)(genPtr->rset);
          (*rset->process) ( genPtr );
				}
        dbScanUnlock( genPtr );
      }

      if ( !force && ( rval1 == ptr->rval ) ) {
        return ORNL_PLC5_IGNORE;
      }
#if 1
      printf( "devOrnlPLC5CheckBoDirect %s - new = %d, last = %d\n", 
				((boRecord *) ptr)->name, rval1, ptr->rval );
#endif
      ptr->rval = rval1;
      ptr->val = rval1;

      ((ornlPLC5DevPtr) ptr->dpvt)->checkFlag = 1;

      return ORNL_PLC5_SUCCESS;

    }
    return ORNL_PLC5_IGNORE;
  }
  return ORNL_PLC5_FAIL;

}

static int boReadIniVal (
  struct boRecord *ptr
) {

int stat, rval;
ornlPLC5DevHandle handle = (ornlPLC5DevHandle) ptr->dpvt;


  stat = ornlPLC5ReadBit( handle, &rval );
  if ( !( stat & 1 ) ) {
    recGblSetSevr( ptr, WRITE_ALARM, INVALID_ALARM);
    if ( plc5Msgs() ) printf( "boReadIniVal - ornlPLC5ReadBit failed\n" );
    return ORNL_PLC5_FAIL;
  }

  ptr->rval = rval;

#if 0
  printf( "boReadIniVal - %s rval %d\n", ((ornlPLC5DevPtr) handle)->plc5Addr, rval);
#endif

  return ORNL_PLC5_SUCCESS;

}

static int boReadIniValDirect (
  struct boRecord *ptr
) {

int stat, rval;
ornlPLC5DevHandle handle = (ornlPLC5DevHandle) ((ornlPLC5DevPtr) ptr->dpvt)->check;

	if (handle) {
	  stat = ornlPLC5ReadBit( handle, &rval );
  	if ( !( stat & 1 ) ) {
    	recGblSetSevr( ptr, WRITE_ALARM, INVALID_ALARM);
	    if ( plc5Msgs() ) printf( "boReadIniValDirect - ornlPLC5ReadBit failed\n" );
  	  return ORNL_PLC5_FAIL;
	  }

	  ptr->rval = rval;
		
#if 0
  printf( "boReadIniValDirect - %s rval %d\n", ((ornlPLC5DevPtr) handle)->plc5Addr, rval);
#endif

	} else {
	  ptr->rval = 0;
	}
	
  return ORNL_PLC5_SUCCESS;

}

/**
 * The Init function parses out the PLC address and gets a device handle from driver support. For direct
 * type devices, the tail end of the OUTP field is checked to see if there is a scan block number following
 * the PLC address. If missing or -1, then the device is not placed on the Check queue. If present, then
 * the device gets another handle which is for a bit input from the scan block identified by
 * the extra scan block number. This second handle is attached to the ->rpvt address in the 
 * bo data structure.
 */
static long devOrnlPLC5InitBoRecord (
  struct boRecord *ptr
) {

ornlPLC5DevHandle handle;
ornlPLC5DevHandle handleCheck;
char moduleName[31+1], devName[31+1], plc5Addr[31+1], plc5Trans[31+1];
int unit, channel, stat, unitCheck;
struct instio *pinstio;
callBackPtr curCallBack;

  pinstio = (struct instio *) &ptr->out.value;
  ptr->dpvt = (void *) NULL;

#if 0
  printf( "devOrnlPLC5InitBoRecord - parseParams %s\n", pinstio->string);
#endif

  /* parse device params */
  stat = parseParams( pinstio->string, devName, &unit, moduleName, &channel, plc5Addr );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "devOrnlPLC5InitBoRecord - %s - error %d - parseParams - %s\n",
     ptr->name, stat, devName );
    goto err_return;
  }

  if (0 == strcmp(moduleName, "bitd")) {
    /* parse device params */
    stat = parseParamsCheck( pinstio->string, &unitCheck );
    if ( !( stat & 1 ) ) {
      if ( plc5Msgs() ) printf( "devOrnlPLC5InitBoRecord - %s - error %d - parseParams - %s\n",
       ptr->name, stat, devName );
      goto err_return;
    }
/* printf("InitBoRecord %s unitCheck %d\n", pinstio->string, unitCheck);*/
  }
  
  /* Convert octal to decimal for I: and O: */
  strcpy(plc5Trans, plc5Addr);
  transAddr(plc5Trans);
  
  stat = ornlPLC5GetDevHandle( devName, unit, moduleName, channel, plc5Trans, &handle );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "devOrnlPLC5InitBoRecord, handle error %d - ornlPLC5GetDevHandle - %s\n",
     stat, devName);
    goto err_return;
  }

  ptr->dpvt = (void *) handle;

/*	Where we have a direct bo record, and a check scan block specified
	get a second handle for the unitCheck scan block but as a bit input.
	This will be used to compare the bo value with the check value to detect
	if the PLC has changed a bo bit.
*/	
  if ((0 == strcmp(moduleName, "bitd")) && (unitCheck > -1)) {
  	stat = ornlPLC5GetDevHandle( devName, unitCheck, "bit", channel, plc5Addr, &handleCheck );
  	if ( !( stat & 1 ) ) {
    	if ( plc5Msgs() ) printf( "devOrnlPLC5InitBoRecord, scan error %d - ornlPLC5GetDevHandle - %s\n",
    	 stat, devName);
    	goto err_return;
  	}
#if 1
	  ((ornlPLC5DevPtr) ptr->dpvt)->check = (ornlPLC5DevHandle) handleCheck;
	  /* if the PV specifies PINI as YES, then set the checkFlag so that on
	     processing at init, it will not write to the output
	*/
	((ornlPLC5DevPtr) ptr->dpvt)->checkFlag = (ptr->pini ? 1 : 0);
#endif		
	} else {
#if 1
	  ((ornlPLC5DevPtr) ptr->dpvt)->check = NULL;
#endif
	}

/**
 * Add the record into the scan list for output checking if unitCheck is greater than -1
 */
  if (unitCheck > -1) {
		curCallBack = (callBackPtr) calloc( 1, sizeof(callBackType) );
  	if ( curCallBack ) {
			if ( strcmp( moduleName, "bitd" ) == 0 ) {
				curCallBack->cf = devOrnlPLC5CheckBoDirect;
    	} else {
				curCallBack->cf = devOrnlPLC5CheckBo;
			}
    	curCallBack->rec = (dbCommon *) ptr;
    	callBackTail->flink = curCallBack;
    	callBackTail = curCallBack;
    	callBackTail->flink = NULL;
  	}
	} else {
/*    printf("Skipped adding %s into bo scan list\n", plc5Addr);*/
  }
	
	if ( strcmp( moduleName, "bitd" ) == 0 ) {
		stat = boReadIniVal( ptr );
/*  	stat = boReadIniValDirect( ptr ); */
	} else {
	  stat = boReadIniVal( ptr );
	}

  if ( stat & 1 ) return 0; /* calc ini val from rval */

  return 2; /* don't calc ini val from rval */

err_return:
	printf("InitBoRecord error return %s\n", pinstio->string);
  ptr->dpvt = (void *) NULL;
  return -1;

}

static long devOrnlPLC5WriteBo (
        struct boRecord *ptr
        ) {

    int stat;
    ornlPLC5DevHandle handle = (ornlPLC5DevHandle) ptr->dpvt;


    if ( ptr->pact ) return 0;

    /* if the check flag is set we don't do a write, but the record will still process
       The check flag is set in the check scan routine for any changed Bo PV */

    if (((ornlPLC5DevPtr) ptr->dpvt)->checkFlag != 0) {

#if 0
        printf( "devOrnlPLC5WriteBo %s check process (no write to PLC)\n", ptr->name);
#endif

        ((ornlPLC5DevPtr) ptr->dpvt)->checkFlag = 0;
        return 0;
    }

#ifdef ORNL_DISA_WARM_START
    if ( ptr->disa == 0 ) {
        return 2; /* don't convert */
    }
#endif

#if 0
    printf( "devOrnlPLC5WriteBo %p\n", handle);
#endif


    stat = ornlPLC5WriteBit( handle, (int) ptr->rval );
    if ( !( stat & 1 ) ) {
        recGblSetSevr( ptr, WRITE_ALARM, INVALID_ALARM);
    }

    return 0;

}

