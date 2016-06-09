#ifndef __enetPlc5_linux_h
#define __enetPlc5_linux_h 1

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
#include "epicsMutex.h"

#include "ornl_sys_types.h"
#include "ornl_thread.h"

#define ENETPLC5_E_SUCCESS 1
#define ENETPLC5_E_NOT_IMPL 2
#define ENETPLC5_E_UNIX_ERROR 100
#define ENETPLC5_E_HOST_NOT_FOUND 102
#define ENETPLC5_E_CONNECT_FAILED 104
#define ENETPLC5_E_CONNECT_REFUSED 106
#define ENETPLC5_E_BAD_ADDR_TYPE 108
#define ENETPLC5_E_BAD_ADDR_LEN 110
#define ENETPLC5_E_NOT_CONNECTED 112
#define ENETPLC5_E_PLC_BAD_ADDR 114
#define ENETPLC5_E_UNEXPECTED 116
#define ENETPLC5_E_PLC_READ_FAIL 118
#define ENETPLC5_E_PLC_BAD_CONID 120
#define ENETPLC5_E_PLC_BAD_TNS 122
#define ENETPLC5_E_TIMEOUT 124
#define ENETPLC5_E_INVALID_PARAM 126
#define ENETPLC5_E_NOMEM 128
#define ENETPLC5_E_CON_RESET 130
#define ENETPLC5_E_CON_CLOSED 132

typedef void *enetPlc5Comm;

int enetPlc5Init (
  enetPlc5Comm *handle
);

int enetPlc5Destroy (
  enetPlc5Comm *handle
);

unsigned int enetPlc5InetAddr (
  enetPlc5Comm *handle
);

void enetPlc5SetTimeout (
  enetPlc5Comm handle,
  double seconds
);

int enetPlc5ConnectPlc (
  enetPlc5Comm handle,
  char *hostIpAddr
);

int enetPlc5DisconnectPlc (
  enetPlc5Comm handle
);

int enetPlc5ReadPlc (
  enetPlc5Comm handle,
  char *addr,
  unsigned int num,
  short *words,
  epicsMutexId lock
);

int enetPlc5ReadPlcBit (
  enetPlc5Comm handle,
  char *addr,
  int * state,
  epicsMutexId lock
);

int enetPlc5WritePlc (
  enetPlc5Comm handle,
  char *addr,
  unsigned int num,
  short *words,
  epicsMutexId lock
);

int enetPlc5WritePlcWord (
  enetPlc5Comm handle,
  char *addr,
  short *words,
  epicsMutexId lock
);

int enetPlc5WritePlcBit (
  enetPlc5Comm handle,
  char *addr,
  int state,
  epicsMutexId lock
);

int enetPlc5GetUnixError (
  enetPlc5Comm handle
);

void plc5WriteWord (
  char *ip,
  char *addr,
  char *word
);

void plc5WriteBit (
  char *ip,
  char *addr,
  char *set
);

void plc5ReadWord (
  char *ip,
  char *addr
);

void plc5ReadBit (
  char *ip,
  char *addr
);

int transAddr(
  char *addr
);

#endif
