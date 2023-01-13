#ifndef __deviceManager_h
#define __deviceManager_h 1

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include <iocsh.h>
#include <epicsExport.h>

#include "ornl_avl.h"

#include "dbCommon.h"
#define epicsExportSharedSymbols

long epicsShareAPI devMgrInit (
  char *fileName
);


typedef struct propFieldTag {
  char *name;         /* for reports, prompts, etc. */
  char *value;
  char *value2;      /* for mem addr and io addr */
  long dataType;     /* 1=char, 2=int, 3=float (but, always stored as char) */
} propFieldType;

typedef struct genericPropListTag {
  AVL_FIELDS(propListTag)
  char *unit;
  char *deviceName; /* op sys device name */

  /* these specify independent properties of a single device */
  propFieldType prop0;
  propFieldType prop1;
  propFieldType prop2;
  propFieldType prop3;
  propFieldType prop4;
  propFieldType prop5;
  propFieldType prop6;
  propFieldType prop7;
  propFieldType prop8;
  propFieldType prop9;

  /* these all must be unique */
  propFieldType mem0;
  propFieldType mem1;
  propFieldType mem2;

  /* these all must be unique */
  propFieldType io0;
  propFieldType io1;
  propFieldType io2;

  /* these all must be unique */
  propFieldType int0;
  propFieldType int1;
  propFieldType int2;

  /* these all must be unique */
  propFieldType dma0;
  propFieldType dma1;
  propFieldType dma2;

  /* the combination of these two must be unique */
  propFieldType bus;
  propFieldType node;

} propListType, *propListPtr;

typedef struct plc5EnetPropListTag {
  AVL_FIELDS(propListTag)
  char *unit;
  char *devicePostfix; /* op sys device name postfix */
  propFieldType ipAddr;
  propFieldType ipPort;
  propFieldType plc5TblAddr;
  propFieldType plc5TblLen;
  propFieldType scanRate; /* seconds, 0 = on change if direction = output */
  propFieldType direction; /* input or output */
} plc5EnetPropListType, *plc5EnetPropListPtr;

typedef struct netSerialPropListTag {
  AVL_FIELDS(propListTag)
  char *unit;
  char *devicePostfix; /* op sys device name postfix */
  propFieldType ipAddr;
  propFieldType ipPort;
  propFieldType ipProto;
  propFieldType address;
} netSerialPropListType, *netSerialPropListPtr;

typedef struct serialPropListTag {
  AVL_FIELDS(propListTag)
  char *unit;
  char *devicePostfix; /* op sys device name postfix */
  propFieldType baud;
  propFieldType dataBits;
  propFieldType stopBits;
  propFieldType parity;
} serialPropListType, *serialPropListPtr;

typedef struct df1PropListTag {
  AVL_FIELDS(propListTag)
  char *unit;
  char *desc;       /* description (for report) */
  char *devicePostfix; /* op sys device name postfix */
  propFieldType baud;
  propFieldType dataBits;
  propFieldType stopBits;
  propFieldType parity;
  propFieldType address;
} df1PropListType, *df1PropListPtr;

typedef struct devListTag {
  AVL_FIELDS(devListTag)
  char *name;
  char *className;  /* serial, df1, generic, etc. */
  AVL_HANDLE properties;
} devListType, *devListPtr;


int devMgrGetFirst (
  char *name,
  propListPtr *ptr
);

int devMgrGetNext (
  char *name,
  propListPtr *ptr
);

int devMgrGetFirstFromAll (
  char **className,
  char **name,
  propListPtr *ptr
);

int devMgrGetNextFromAll (
  char **className,
  char **name,
  propListPtr *ptr
);

void devMgrGenReport ( void );

void devMgrGenReport2 ( void );

#endif
