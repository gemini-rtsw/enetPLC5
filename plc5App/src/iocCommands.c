#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iocsh.h>
#include <epicsExport.h>

#include "pls.h"
#include "devManager.h"
#include "drvOrnlPLC5.h"

long epicsShareAPI snap_db (
  char *arg1,
  char *arg2,
  char *arg3
);


/* for ornlPLC5 */

static void cpm (
  const iocshArgBuf *args
) {

  configPLC5Modules( args[0].sval );

}

static void opsm (
  const iocshArgBuf *args
) {

  int flag = 0;

  if ( args ) {
    if (args[0].sval ) {
      flag = atol( args[0].sval );
    }
  }
  ornlPLC5ShowMsgs( flag );

}

static void opdshow (
  const iocshArgBuf *args
) {

  ornlPLC5DebugShow();

}

static void opdon (
  const iocshArgBuf *args
) {

int i;

  i = atol( args[0].sval );

  ornlPLC5DebugOn( i );

}

static void opdoff (
  const iocshArgBuf *args
) {

  ornlPLC5DebugOff();

}

static void opzc (
  const iocshArgBuf *args
) {

  ornlPLC5ZeroCounters();

}

static void opsc (
  const iocshArgBuf *args
) {

  ornlPLC5ShowCounters();

}

static void opswb (
  const iocshArgBuf *args
) {

  ornlPLC5ShowWriteBuffers();

}

static void pls (
  const iocshArgBuf *args
) {

  pvlistserver( args[0].sval );

}

static void dmi (
  const iocshArgBuf *args
) {

long stat;

  stat = devMgrInit( args[0].sval );

}

static void dmgr (
  const iocshArgBuf *args
) {

  devMgrGenReport2();

}

static void snap (
  const iocshArgBuf *args
) {

  snap_db( args[0].sval, args[1].sval, args[2].sval );

}

static void wrb (
  const iocshArgBuf *args
) {

  if (args && args[0].sval && args[1].sval && args[2].sval) {
    plc5WriteBit( args[0].sval, args[1].sval, args[2].sval );
  } else {
    printf("usage: plc5WriteBit IP Addr 1/0\n");
  }
}

static void rdb (
  const iocshArgBuf *args
) {

  plc5ReadBit( args[0].sval, args[1].sval );

}

static void wrw (
  const iocshArgBuf *args
) {

  plc5WriteWord( args[0].sval, args[1].sval, args[2].sval );

}

static void rdw (
  const iocshArgBuf *args
) {

  plc5ReadWord( args[0].sval, args[1].sval );

}

static const iocshArg arg0 = {
  "one",
  iocshArgString
};

static const iocshArg arg1 = {
  "two",
  iocshArgString
};

static const iocshArg arg2 = {
  "three",
  iocshArgString
};

static const iocshArg *const args[3] = {
  &arg0,
  &arg1,
  &arg2
};

static iocshFuncDef plsFuncDef = {
  "pvlistserver",
  1,
  args
};

static iocshFuncDef devMgrFuncDef = {
  "devMgrInit",
  1,
  args
};

static iocshFuncDef devMgrRptFuncDef = {
  "devMgrGenReport2",
  0,
  args
};

static iocshFuncDef cpmshowFuncDef = {
  "configPLC5Modules",
  1,
  args
};

static iocshFuncDef opsmFuncDef = {
  "ornlPLC5ShowMsgs",
  1,
  args
};

static iocshFuncDef opdshowFuncDef = {
  "ornlPLC5DebugShow",
  0,
  args
};

static iocshFuncDef opdonFuncDef = {
  "ornlPLC5DebugOn",
  1,
  args
};

static iocshFuncDef opdoffFuncDef = {
  "ornlPLC5DebugOff",
  0,
  args
};

static iocshFuncDef opzcFuncDef = {
  "ornlPLC5ZeroCounters",
  0,
  args
};

static iocshFuncDef opscFuncDef = {
  "ornlPLC5ShowCounters",
  0,
  args
};

static iocshFuncDef opswbFuncDef = {
  "ornlPLC5ShowWriteBuffers",
  0,
  args
};

static iocshFuncDef snapDbFuncDef = {
  "snap",
  3,
  args
};

static iocshFuncDef wrbDbFuncDef = {
  "plc5WriteBit",
  3,
  args
};

static iocshFuncDef rdbDbFuncDef = {
  "plc5ReadBit",
  2,
  args
};

static iocshFuncDef wrwDbFuncDef = {
  "plc5WriteWord",
  3,
  args
};

static iocshFuncDef rdwDbFuncDef = {
  "plc5ReadWord",
  2,
  args
};

static void iocCommandsRegistrar(void)
{

  iocshRegister( &plsFuncDef, pls );

  iocshRegister( &devMgrFuncDef, dmi );
  iocshRegister( &devMgrRptFuncDef, dmgr );

  iocshRegister( &cpmshowFuncDef, cpm );
  iocshRegister( &opsmFuncDef, opsm );
  iocshRegister( &opdshowFuncDef, opdshow );
  iocshRegister( &opdonFuncDef, opdon );
  iocshRegister( &opdoffFuncDef, opdoff );
  iocshRegister( &opzcFuncDef, opzc );
  iocshRegister( &opscFuncDef, opsc );
  iocshRegister( &opswbFuncDef, opswb );

  iocshRegister( &snapDbFuncDef, snap );

  iocshRegister( &wrbDbFuncDef, wrb );
  iocshRegister( &rdbDbFuncDef, rdb );
  iocshRegister( &wrwDbFuncDef, wrw );
  iocshRegister( &rdwDbFuncDef, rdw );

}
epicsExportRegistrar(iocCommandsRegistrar);
