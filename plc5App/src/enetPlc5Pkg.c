/**
 * Note that all PLC addressing in this module is decimal (binary)
 * This means any octal addresses for I: and O: registers must be
 * converted before being sent to the Write and Read commands
 *
 * The command line utilities at the end of the module translate
 * from Octal to Binary so that the user can enter Octal numbers
 * for I: and O: type addresses.
 */
#include "enetPlc5Priv.h"
#include <strings.h>
#include <sys/select.h>


/* ============================================================== */

static THREAD_HANDLE g_delay;

static void showInfo (
  int stat,
  char *file,
  int line
) {

  int e = errno;
  time_t curtime = time(NULL);

  if ( stat == ENETPLC5_E_UNIX_ERROR ) {
    if ( plc5Msgs() ) printf( "%s Unix error %-d in [%s] at line %-d\n", asctime(localtime(&curtime)) , e, file, line );
  }
  else {
    if ( plc5Msgs() ) printf( "%s Error %-d in [%s] at line %-d\n", asctime(localtime(&curtime)), stat, file, line );
  }

}

static void delay (
  int ms
) {

  double s = (double) ms / 1000.0;

  thread_delay( g_delay, s );

}

/* public */

int enetPlc5Init (
  enetPlc5Comm *handle
) {

privEnetPlc5CommPtr ptr;
int status;

  ptr = (privEnetPlc5CommPtr) calloc( 1, sizeof(privEnetPlc5CommType) );
  if ( !ptr ) {
    return ENETPLC5_E_NOMEM;
  }

  status = thread_create_handle( &g_delay, NULL );
  if ( !( status & 1 ) ) return status;

  ptr->plcType = 0;
  ptr->connected = 0;
  ptr->tns = 0;
  ptr->readReq = NULL;
  ptr->writeReq = NULL;
  ptr->writeBitReq = NULL;
  ptr->unixErrCode = 0;
  enetPlc5SetTimeout( ptr, 60.0 );

  *handle = (enetPlc5Comm) ptr;

  return ENETPLC5_E_SUCCESS;

}

int enetPlc5Destroy (
  enetPlc5Comm *handle
) {

privEnetPlc5CommPtr ptr = (privEnetPlc5CommPtr) *handle;

  if ( ptr ) {
    free( ptr );
    *handle = NULL;
  }

  return ENETPLC5_E_SUCCESS;

}

unsigned int enetPlc5InetAddr (
  enetPlc5Comm *handle
) {

  privEnetPlc5CommPtr ptr = (privEnetPlc5CommPtr) *handle;

  return (unsigned int) ptr->sockAddr.sin_addr.s_addr;

}

void enetPlc5SetTimeout (
  enetPlc5Comm handle,
  double seconds
) {

privEnetPlc5CommPtr ptr = (privEnetPlc5CommPtr) handle;

  sys_cvt_seconds_to_timeout( (float) seconds, &ptr->timeout );

}

int enetPlc5ConnectPlc (
  enetPlc5Comm handle,
  char *hostIpAddr
) {

int stat, stat1, n, value, len, nTry, plcStat, ipAddr;
unsigned short conPort;

enetHdrType hdr;
enetMsgType rcvBuf;


privEnetPlc5CommPtr ptr = (privEnetPlc5CommPtr) handle;

/*  If readReq or writeReq exists (is non-null) then delete. This will
 *  be true if peer has disconnected and we are attempting to reconnect
 */
  if ( ptr->readReq ) {
    free( ptr->readReq );
    ptr->readReq = NULL;
  }
  if ( ptr->writeReq ) {
    free( ptr->writeReq );
    ptr->writeReq = NULL;
  }
  if ( ptr->writeBitReq ) {
    free( ptr->writeBitReq );
    ptr->writeBitReq = NULL;
  }

  ptr->sockfd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
  if ( ptr->sockfd == -1 ) {
    ptr->unixErrCode = errno;
    stat = ENETPLC5_E_UNIX_ERROR;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  value = 1;
  len = sizeof(value);
  stat = setsockopt( ptr->sockfd, IPPROTO_TCP, TCP_NODELAY,
   (char *) &value, len );

  value = 1;
  len = sizeof(value);
  stat = setsockopt( ptr->sockfd, SOL_SOCKET, SO_KEEPALIVE,
   (char *) &value, len );


  /* Establish connection */
  nTry = MAX_CONNECT_TRIES;
  ptr->connected = 0;
  conPort = htons( (short) 2222 );
  ipAddr = inet_addr( hostIpAddr );
  do {

    bzero( (char *) &ptr->sockAddr, sizeof(ptr->sockAddr) );
    ptr->sockAddr.sin_family = AF_INET;
    ptr->sockAddr.sin_addr.s_addr = ipAddr;
    ptr->sockAddr.sin_port = conPort;

    stat = connect( ptr->sockfd, (struct sockaddr *) &ptr->sockAddr,
     sizeof(ptr->sockAddr) );
    if ( stat == 0 ) {
      ptr->connected = 1;
    }
    else {
      nTry--;
      if ( errno == ECONNREFUSED ) {
        delay( 1000 );
        continue;
      }
      else {
        ptr->unixErrCode = errno;
        stat = ENETPLC5_E_UNIX_ERROR;
        showInfo( stat, __FILE__, __LINE__ );
        goto errReturn;
      }
    }

  } while ( !ptr->connected && ( nTry > 0 ) );

  if ( !ptr->connected ) {

    stat = ENETPLC5_E_CONNECT_REFUSED;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

#if 0
  printf("enetPlc5ConnectPlc connected %s\n", hostIpAddr);
#endif

  /* Initial connection dialog */

  memset( (void *) &hdr, 0, sizeof(enetHdrType) );
  hdr.mode = 1;
  hdr.subMode = INIT_CONNECT;
  hdr.conId = 0;
  hdr.pccc_length = 0;
  hdr.custom.version = htons(PCCC_VERSION);
  hdr.custom.backlog = htons(PCCC_BACKLOG);

  len = sizeof(enetHdrType);
  stat = sendPlcData( ptr, (char *) &hdr, len, &n );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  stat = getPlcData( ptr, (char *) &rcvBuf, 255, 28, enetStatusOffset,
   28, 0, &n );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* Check results */

  if ( n != 28 ) {
    stat = ENETPLC5_E_CONNECT_FAILED;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  plcStat = ntohl( rcvBuf.hdr.status );
  if ( plcStat != 0 ) {
    stat = ENETPLC5_E_CONNECT_FAILED;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  ptr->conId = rcvBuf.hdr.conId; /* leave this in network byte order */

  return ENETPLC5_E_SUCCESS;

errReturn:

printf("Error enetPlc5ConnectPlc %s\n", hostIpAddr);


  if ( ptr->connected ) {
    ptr->connected = 0;
    stat1 = shutdown( ptr->sockfd, 2 );
    if ( stat1 ) perror( "" );
  }

  if ( ptr->sockfd != -1 ) {
    stat1 = close( ptr->sockfd );
    if ( stat1 ) perror( "" );
    ptr->sockfd = -1;
  }

  return stat;

}

int enetPlc5DisconnectPlc (
  enetPlc5Comm handle
) {

privEnetPlc5CommPtr ptr = (privEnetPlc5CommPtr) handle;
int stat, retStat;

  retStat = ENETPLC5_E_SUCCESS;

  if ( ptr->connected ) {
    ptr->connected = 0;
    stat = shutdown( ptr->sockfd, 2 );
    if ( stat ) {
      perror( "shutdown" );
      ptr->unixErrCode = errno;
      retStat = ENETPLC5_E_UNIX_ERROR;
    }
    stat = close( ptr->sockfd );
    if ( stat ) {
      perror( "close" );
      ptr->unixErrCode = errno;
      retStat = ENETPLC5_E_UNIX_ERROR;
    }
  }
  else {
    retStat = ENETPLC5_E_NOT_CONNECTED;
  }

  return retStat;

}

int enetPlc5ReadPlc (
  enetPlc5Comm handle,
  char *addr,
  unsigned int num,
  short *words,
  epicsMutexId lock
) {

privEnetPlc5CommPtr ptr = (privEnetPlc5CommPtr) handle;

readBlockReplyType rcvBuf;

int n, stat, fileType, fileNum, offset, bit, numBytesSent, numBytesRcvd;
unsigned int i, ii, iii, numBytes;

#if 0
  printf("enetPlc5ReadPlc handle %p addr %s num %d\n", handle, addr, num);
#endif
  

  if ( !ptr->readReq ) {

/* printf("enetPlc5ReadPlc setup %s\n", addr);*/

    ptr->readReq = (readBlockReqPtr) calloc( 1, sizeof(readBlockReqType) );

    ptr->readReq->mode = 1;
    ptr->readReq->subMode = 7;

    /* conId is already in network byte order */
    ptr->readReq->conId = ptr->conId;

    /* generate this unique value system wide */
    ptr->readReq->request_id = htonl(0xe87573);

    ptr->readReq->name_id = 0;

    ptr->readReq->src = 0;
    ptr->readReq->control = 5;
    ptr->readReq->dst = 0;
    ptr->readReq->lsap = 0;
    ptr->readReq->cmd = 0x0f;
    ptr->readReq->sts = 0;
    ptr->readReq->fnc = 0x68;

    ptr->readReq->offset[0] = 0;
    ptr->readReq->offset[1] = 0;

  }
  ptr->readReq->trans[0] = (unsigned char) ( num & (unsigned char) 0xff );
  ptr->readReq->trans[1] = (unsigned char) ( ( num / 256 ) & (unsigned char) 0xff );

#if 0 /* DBM */
if (strstr(addr, "T4")) {
printf("Read %s num %d\n", addr, num);
}
#endif

  stat = parseAddress( addr, &fileType, &fileNum, &offset, &bit );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    printf("Address %s\n", addr);
    goto errReturn;
  }


#if 0 /* DBM */
if (strstr(addr, "T4")) {
printf("Read %s offset %d bit %d\n", addr, offset, bit);
}
#endif

  n = 0;
  stat = encodeDf1AddrOfs( ptr, fileNum, offset, &n, ptr->readReq->data );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

    /* append transaction count to data */
    ptr->readReq->data[n] =
     (unsigned char) ( num & (unsigned char) 0xff ); n++;
    ptr->readReq->data[n] =
     (unsigned char) ( ( num / 256 ) & (unsigned char) 0xff ); n++;

    ptr->readReqLen = readBlockReqEnetHdrSize + readBlockReqMsgHdrSize + n;

    ptr->readReq->pccc_length = htons( readBlockReqMsgHdrSize + n );

    ptr->replyNumBytesExpected = readBlockReplyEnetHdrSize +
     readBlockReplyMsgHdrSize + 4 + num + num;

    ptr->replyNumBytesExpectedOnError = readBlockReplyEnetHdrSize +
     readBlockReplyMsgHdrSize;


  ptr->readReq->status = 0;

  /* get next transaction sequence number */
  ptr->readReq->tns = htons(getTns(ptr));

#if 0 /* DBM */
if (strstr(addr, "T4")) {
	dumpWrite((unsigned char*) ptr->readReq, ptr->readReqLen);
}
#endif

  stat = sendPlcData( ptr, (char *) ptr->readReq, ptr->readReqLen,
   &numBytesSent );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error from sendPlcData, stat = %-d\n", stat );
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }


  stat = getPlcData( ptr, (char *) &rcvBuf, 2048, ptr->replyNumBytesExpected,
   readBlockReplyStatusOffset, ptr->replyNumBytesExpectedOnError, 0,
   &numBytesRcvd );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error from getPlcData, stat = %-d\n", stat );
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

#if 0 /* DBM */
if (strstr(addr, "T4")) {
	printf("Read T4 numBytes %d expected %d\n", numBytesRcvd, ptr->replyNumBytesExpected);
	dumpWrite((unsigned char*) &rcvBuf, numBytesRcvd);
}
#endif

  if ( rcvBuf.sts != 0 ) {
    if ( plc5Msgs() ) printf( "sin_addr = %-x [hex]\n", (unsigned int)(ptr->sockAddr.sin_addr.s_addr ));
    if ( plc5Msgs() ) printf( "rcvBuf.sts = %-d\n", rcvBuf.sts );
    if ( rcvBuf.sts == 0xf0 ) {
      if ( plc5Msgs() ) printf( "ext sts = %-d\n", (int) rcvBuf.a );
    }
    stat = ENETPLC5_E_PLC_READ_FAIL;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* a few checks */

  /*these are both in network byte order */
  if ( ptr->readReq->conId != rcvBuf.conId ) {
    stat = ENETPLC5_E_PLC_BAD_CONID;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

   /*these are both in network byte order */
  if ( ptr->readReq->tns != rcvBuf.tns ) {
    stat = ENETPLC5_E_PLC_BAD_TNS;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* for our application, rcvBuf.a should always be 0x9a; */
  if ( rcvBuf.a != 0x9a ) {
    stat = ENETPLC5_E_UNEXPECTED;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* extract the data */

  numBytes = rcvBuf.bdata[1] + rcvBuf.bdata[2] * 256 - 1;

#if 0 /* DBM */
if (strstr(addr, "T4")) {
	printf("Read T4 numBytes %d\n", numBytes);
	dumpWrite((unsigned char*) &rcvBuf, numBytesRcvd);
}
#endif


  if ( lock ) epicsMutexLock( lock );
  for ( i=0, ii=4, iii=0; i<numBytes; i+=2, ii+=2, iii++ ) {
    words[iii] = (short) ( (unsigned char) rcvBuf.bdata[ii] +
     (char) rcvBuf.bdata[ii+1] * 256 );
  }
  if ( lock ) epicsMutexUnlock( lock );

/*  printf("enetPlc5ReadPlc success %s numBytes %d\n", addr, numBytes); */
  return ENETPLC5_E_SUCCESS;

errReturn:

/*  printf("enetPlc5ReadPlc error %s\n", addr); */

  return stat;

}

int enetPlc5WritePlc (
  enetPlc5Comm handle,
  char *addr,
  unsigned int num,
  short *words,
  epicsMutexId lock
) {

privEnetPlc5CommPtr ptr = (privEnetPlc5CommPtr) handle;

wrtBlockReplyType rcvBuf;

int n, stat, fileType, fileNum, offset, bit, numBytesSent, numBytesRcvd;
unsigned int i, ii, numBytes;

#if 1
  printf("enetPlc5WritePlc addr %s num %d word0 0x%x\n", addr,num,words[0]);
#endif


  if ( !ptr->writeReq ) {

#if 0
  printf("enetPlc5WritePlc setup\n");
#endif

    ptr->writeReq = (wrtBlockReqPtr) calloc( 1, sizeof(wrtBlockReqType) );

    stat = parseAddress( addr, &fileType, &fileNum, &offset, &bit );
    if ( !( stat & 1 ) ) {
      showInfo( stat, __FILE__, __LINE__ );
      printf("Address %s\n", addr);
      goto errReturn;
    }

    ptr->writeReq->mode = 1;
    ptr->writeReq->subMode = 7;

    /* conId is already in network byte order */
    ptr->writeReq->conId = ptr->conId;

    /* generate this unique value system wide */
    ptr->writeReq->request_id = htonl(0xe87573);

    ptr->writeReq->name_id = 0;

    ptr->writeReq->src = 0;
    ptr->writeReq->control = 5;
    ptr->writeReq->dst = 0;
    ptr->writeReq->lsap = 0;
    ptr->writeReq->cmd = 0x0f;
    ptr->writeReq->sts = 0;
    ptr->writeReq->fnc = 0x67;

    ptr->writeReq->offset[0] = 0;
    ptr->writeReq->offset[1] = 0;
    ptr->writeReq->trans[0] = (unsigned char) ( num & (unsigned char) 0xff );
    ptr->writeReq->trans[1] =
     (unsigned char) ( ( num / 256 ) & (unsigned char) 0xff );

    n = 0;
    stat = encodeDf1AddrOfs( ptr, fileNum, offset, &n, ptr->writeReq->data );
    if ( !( stat & 1 ) ) {
      showInfo( stat, __FILE__, __LINE__ );
      goto errReturn;
    }

    ptr->writeReq->data[n] = 0x9a; n++; /* flag byte: */
    /* bits 4-7: data type id in next one byte ( 9 = array ) */
    /* bits 0-3: bytes per data type = 2 ( 2 data bytes + 1 descriptor ) */

    ptr->writeReq->data[n] = 9; n++; /* type = array */

    numBytes = num + num + 1; /* 2 bytes per int * num ints + 1 descriptor */
    ptr->writeReq->data[n] =
     (unsigned char) ( numBytes & (unsigned char) 0xff ); n++;
    ptr->writeReq->data[n] =
     (unsigned char) ( ( numBytes / 256 ) & (unsigned char) 0xff ); n++;

    /* array descriptor: type = integer, 2 bytes per element */
    ptr->writeReq->data[n] = 0x42; n++;

    ptr->writeReqDataIndex = n;

    if ( lock ) epicsMutexLock( lock );
    for ( ii=0, i=n; ii<num; i+=2, ii++ ) {
      ptr->writeReq->data[i] =
       (unsigned char) ( words[ii] & (unsigned char) 0xff );
      ptr->writeReq->data[i+1] =
       (unsigned char) ( ( words[ii] >> 8 ) & (unsigned char) 0xff );
    }
    if ( lock ) epicsMutexUnlock( lock );

    n += num + num;

    ptr->writeReqLen = wrtBlockReqEnetHdrSize + wrtBlockReqMsgHdrSize + n;

    ptr->writeReq->pccc_length = htons( wrtBlockReqMsgHdrSize + n );

    ptr->replyNumBytesExpected = wrtBlockReplyEnetHdrSize +
     wrtBlockReplyMsgHdrSize;

    ptr->replyNumBytesExpectedOnError = wrtBlockReplyEnetHdrSize +
     wrtBlockReplyMsgHdrSize;

  }
  else {

    n = ptr->writeReqDataIndex;

    if ( lock ) epicsMutexLock( lock );
    for ( ii=0, i=n; ii<num; i+=2, ii++ ) {
      ptr->writeReq->data[i] =
       (unsigned char) ( words[ii] & (unsigned char) 0xff );
      ptr->writeReq->data[i+1] =
       (unsigned char) ( ( words[ii] >> 8 ) & (unsigned char) 0xff );
    }
    if ( lock ) epicsMutexUnlock( lock );

  }

  ptr->writeReq->status = 0;

  /* get next transaction sequence number */
  ptr->writeReq->tns = htons(getTns(ptr));

  stat = sendPlcData( ptr, (char *) ptr->writeReq, ptr->writeReqLen,
   &numBytesSent );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  stat = getPlcData( ptr, (char *) &rcvBuf, 2048, ptr->replyNumBytesExpected,
   wrtBlockReplyStatusOffset, ptr->replyNumBytesExpectedOnError, 1,
   &numBytesRcvd );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  if ( rcvBuf.sts != 0 ) {
    stat = ENETPLC5_E_PLC_READ_FAIL;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* a few checks */

  /*these are both in network byte order */
  if ( ptr->writeReq->conId != rcvBuf.conId ) {
    stat = ENETPLC5_E_PLC_BAD_CONID;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* these are both in network byte order */
  if ( ptr->writeReq->tns != rcvBuf.tns ) {
    stat = ENETPLC5_E_PLC_BAD_TNS;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  return ENETPLC5_E_SUCCESS;

errReturn:

  return stat;

}


int enetPlc5WritePlcWord (
  enetPlc5Comm handle,
  char *addr,
  short *words,
  epicsMutexId lock
) {

privEnetPlc5CommPtr ptr = (privEnetPlc5CommPtr) handle;

wrtBlockReplyType rcvBuf;

int n, stat, fileType, fileNum, offset, bit, numBytesSent, numBytesRcvd;
unsigned int numBytes;
unsigned int num = 1;

#if 0
  printf("enetPlc5WritePlcWord addr %s num %d word[0] %d\n", addr,num,words[0]);
#endif


  if ( !ptr->writeReq ) {

#if 0
  printf("enetPlc5WritePlcWord setup\n");
#endif

    ptr->writeReq = (wrtBlockReqPtr) calloc( 1, sizeof(wrtBlockReqType) );

    ptr->writeReq->mode = 1;
    ptr->writeReq->subMode = 7;

    /* conId is already in network byte order */
    ptr->writeReq->conId = ptr->conId;

    /* generate this unique value system wide */
    ptr->writeReq->request_id = htonl(0xe87573);

    ptr->writeReq->name_id = 0;

    ptr->writeReq->src = 0;
    ptr->writeReq->control = 5;
    ptr->writeReq->dst = 0;
    ptr->writeReq->lsap = 0;
    ptr->writeReq->cmd = 0x0f;
    ptr->writeReq->sts = 0;
    ptr->writeReq->fnc = 0x67;

    ptr->writeReq->offset[0] = 0;
    ptr->writeReq->offset[1] = 0;
	 
	}

#if 0
printf("WritePlcWord parse\n");
#endif
	
    stat = parseAddress( addr, &fileType, &fileNum, &offset, &bit );
    if ( !( stat & 1 ) ) {
      showInfo( stat, __FILE__, __LINE__ );
      printf("Address %s\n", addr);
      goto errReturn;
    }

    ptr->writeReq->trans[0] = (unsigned char) ( num & (unsigned char) 0xff );
    ptr->writeReq->trans[1] =
     (unsigned char) ( ( num / 256 ) & (unsigned char) 0xff );

    n = 0;
    stat = encodeDf1AddrOfs( ptr, fileNum, offset, &n, ptr->writeReq->data );
    if ( !( stat & 1 ) ) {
      showInfo( stat, __FILE__, __LINE__ );
      goto errReturn;
    }

    ptr->writeReq->data[n] = 0x9a; n++; /* flag byte: */
    /* bits 4-7: data type id in next one byte ( 9 = array ) */
    /* bits 0-3: bytes per data type = 2 ( 2 data bytes + 1 descriptor ) */

    ptr->writeReq->data[n] = 9; n++; /* type = array */

    numBytes = num + num + 1; /* 2 bytes per int * num ints + 1 descriptor */
    ptr->writeReq->data[n] =
     (unsigned char) ( numBytes & (unsigned char) 0xff ); n++;
    ptr->writeReq->data[n] =
     (unsigned char) ( ( numBytes / 256 ) & (unsigned char) 0xff ); n++;

    /* array descriptor: type = integer, 2 bytes per element */
    ptr->writeReq->data[n] = 0x42; n++;

    ptr->writeReqDataIndex = n;

/* copy data bytes into message stream */

      ptr->writeReq->data[n] =
       (unsigned char) ( words[0] & (unsigned char) 0xff );
      ptr->writeReq->data[n+1] =
       (unsigned char) ( ( words[0] >> 8 ) & (unsigned char) 0xff );

    n += num + num;

    ptr->writeReqLen = wrtBlockReqEnetHdrSize + wrtBlockReqMsgHdrSize + n;

    ptr->writeReq->pccc_length = htons( wrtBlockReqMsgHdrSize + n );

    ptr->replyNumBytesExpected = wrtBlockReplyEnetHdrSize +
     wrtBlockReplyMsgHdrSize;

    ptr->replyNumBytesExpectedOnError = wrtBlockReplyEnetHdrSize +
     wrtBlockReplyMsgHdrSize;


  ptr->writeReq->status = 0;

  /* get next transaction sequence number */
  ptr->writeReq->tns = htons(getTns(ptr));

  stat = sendPlcData( ptr, (char *) ptr->writeReq, ptr->writeReqLen,
   &numBytesSent );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  stat = getPlcData( ptr, (char *) &rcvBuf, 2048, ptr->replyNumBytesExpected,
   wrtBlockReplyStatusOffset, ptr->replyNumBytesExpectedOnError, 1,
   &numBytesRcvd );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  if ( rcvBuf.sts != 0 ) {
    stat = ENETPLC5_E_PLC_READ_FAIL;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* a few checks */

  /*these are both in network byte order */
  if ( ptr->writeReq->conId != rcvBuf.conId ) {
    stat = ENETPLC5_E_PLC_BAD_CONID;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* these are both in network byte order */
  if ( ptr->writeReq->tns != rcvBuf.tns ) {
    stat = ENETPLC5_E_PLC_BAD_TNS;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  return ENETPLC5_E_SUCCESS;

errReturn:

  return stat;

}


int enetPlc5ReadPlcBit (
  enetPlc5Comm handle,
  char *addr,
  int * state,
  epicsMutexId lock
) {

  int stat, fileType, fileNum, offset, bit;
  short words[8];

  *state = 0;

#if 0
  printf("enetPlc5ReadPlcBit handle %p addr %s\n", handle, addr);
#endif
  
  stat = enetPlc5ReadPlc( handle, addr, 1, words, lock );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  stat = parseAddress( addr, &fileType, &fileNum, &offset, &bit );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    printf("Address %s\n", addr);
    goto errReturn;
  }

  *state = (words[0] & (1 << bit)) ? 1 : 0;

#if 0
  printf("raw %d value %d\n", words[0], *state);
#endif
  	
  return ENETPLC5_E_SUCCESS;

errReturn:

  return stat;
}


int enetPlc5WritePlcBit (
  enetPlc5Comm handle,
  char *addr,
  int state,
  epicsMutexId lock
) {

privEnetPlc5CommPtr ptr = (privEnetPlc5CommPtr) handle;

wrtBitReplyType rcvBuf;

int n, stat, fileType, fileNum, offset, bit, numBytesSent, numBytesRcvd;
unsigned short andMask, orMask;

#if 0
  printf("enetPlc5WritePlcBit %s %d\n", addr,state);
#endif

  if ( !ptr->writeBitReq ) {

#if 0
  printf("enetPlc5WritePlcBit setup\n");
#endif

    ptr->writeBitReq = (wrtBitReqPtr) calloc( 1, sizeof(wrtBitReqType) );


    ptr->writeBitReq->mode = 1;
    ptr->writeBitReq->subMode = 7;

    /* conId is already in network byte order */
    ptr->writeBitReq->conId = ptr->conId;

    /* generate this unique value system wide */
    ptr->writeBitReq->request_id = htonl(0xe87573);

    ptr->writeBitReq->name_id = 0;

    ptr->writeBitReq->src = 0;
    ptr->writeBitReq->control = 5;
    ptr->writeBitReq->dst = 0;
    ptr->writeBitReq->lsap = 0;
    ptr->writeBitReq->cmd = 0x0f;
    ptr->writeBitReq->sts = 0;
    ptr->writeBitReq->fnc = 0x26;

	}

#if 0
  printf("enetPlc5WritePlcBit setup complete\n");
#endif
		
  stat = parseAddress( addr, &fileType, &fileNum, &offset, &bit );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    printf("Address %s\n", addr);
    goto errReturn;
  }

#if 0
 printf("enetPlc5WritePlcBit parse fileType %d fileNum %d offset %d bit %d\n", fileType, fileNum, offset, bit);
#endif

  n = 0;
  stat = encodeDf1AddrOfs( ptr, fileNum, offset, &n, ptr->writeBitReq->data );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* Create AND and OR masks */

  if (state) {
		andMask = ~0;
		orMask = 1 << bit;
	} else {
		andMask = ~(1 << bit);
	 	orMask = 0;
  }	 

#if 0
 printf("enetPlc5WritePlcBit bit %d andMask 0x%x orMask 0x%x\n", bit, andMask, orMask);
#endif

  ptr->writeBitReq->data[n] =
   (unsigned char) ( andMask  & (unsigned char) 0xff ); n++;
  ptr->writeBitReq->data[n] =
   (unsigned char) ( ( andMask  / 256 ) & (unsigned char) 0xff ); n++;
	
  ptr->writeBitReq->data[n] =
   (unsigned char) ( orMask  & (unsigned char) 0xff ); n++;
	
  ptr->writeBitReq->data[n] =
   (unsigned char) ( ( orMask  / 256 ) & (unsigned char) 0xff ); n++;
	
  ptr->writeBitReqDataIndex = n;

  ptr->writeBitReqLen = wrtBitReqEnetHdrSize + wrtBitReqMsgHdrSize + n;

  ptr->writeBitReq->pccc_length = htons( wrtBitReqMsgHdrSize + n );

  ptr->replyNumBytesExpected = wrtBitReplyEnetHdrSize +
   wrtBitReplyMsgHdrSize;

  ptr->replyNumBytesExpectedOnError = wrtBitReplyEnetHdrSize +
   wrtBitReplyMsgHdrSize + 1;

  ptr->writeBitReq->status = 0;

  /* get next transaction sequence number */
  ptr->writeBitReq->tns = htons(getTns(ptr));

  stat = sendPlcData( ptr, (char *) ptr->writeBitReq, ptr->writeBitReqLen,
   &numBytesSent );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  stat = getPlcData( ptr, (char *) &rcvBuf, 2048, ptr->replyNumBytesExpected,
   wrtBitReplyStatusOffset, ptr->replyNumBytesExpectedOnError, 1,
   &numBytesRcvd );
  if ( !( stat & 1 ) ) {
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  if ( rcvBuf.sts != 0 ) {
    stat = ENETPLC5_E_PLC_READ_FAIL;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* a few checks */

  /*these are both in network byte order */
  if ( ptr->writeBitReq->conId != rcvBuf.conId ) {
    stat = ENETPLC5_E_PLC_BAD_CONID;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* these are both in network byte order */
  if ( ptr->writeBitReq->tns != rcvBuf.tns ) {
    stat = ENETPLC5_E_PLC_BAD_TNS;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

#if 0
 printf("enetPlc5WritePlcBit success\n");
#endif

  return ENETPLC5_E_SUCCESS;

errReturn:

  return stat;


}

/* ============================================================== */

/* private */
static int netConnectError (
  int err
) {

  if ( err == 0xd0003 ) return 1;
  if ( err == EPIPE ) return 1;
  if ( err == ENETUNREACH ) return 1;
  if ( err == ENETRESET ) return 1;
  if ( err == ECONNABORTED ) return 1;
  if ( err == ECONNRESET ) return 1;
  if ( err == ENOTCONN ) return 1;
  /* 
   *
   * Deleted. Rippa 06012016 ... Search on RTEMS exploder ...
   * if ( err == ESHUTDOWN ) return 1;
   *
   */
  if ( err == ETIMEDOUT ) return 1;
  if ( err == ENETDOWN ) return 1;
  if ( err == EHOSTUNREACH ) return 1;
  if ( err == EHOSTDOWN ) return 1;

  return 0;

}

static int sendPlcData (
  privEnetPlc5CommPtr ptr,
  char *buf,
  int num,
  int *numSent
) {

  /* Write data to plc. If the operation does not complete in time, */
  /* return a timeout error. */

fd_set fds;
int fd, l, n, stat;

  if ( num < 1 ) {
    stat = ENETPLC5_E_INVALID_PARAM;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  /* flush input buffer */
  flushInput( ptr );

  *numSent = 0;
  l = num;
  while ( *numSent < num ) {

    FD_ZERO( &fds );
    FD_SET( ptr->sockfd, &fds );

    /*fd = select( FD_SETSIZE, (fd_set *) NULL, &fds,*/
    /* (fd_set *) NULL, &ptr->timeout.timeval_time );*/
    {
    struct timeval timeout = { 2, 0 };
    fd = select( FD_SETSIZE, (fd_set *) NULL, &fds,
     (fd_set *) NULL, &timeout );
    }

    if ( fd == 0 ) {
      ornlPLC5IncTimeouts();
      stat = ENETPLC5_E_TIMEOUT;
      showInfo( stat, __FILE__, __LINE__ );
      goto errReturn;
    }
    else if ( fd < 0 ) {
      if ( netConnectError( errno ) ) {
        stat = ENETPLC5_E_CON_RESET;
        showInfo( stat, __FILE__, __LINE__ );
        goto errReturn;
      }
      else {
        ptr->unixErrCode = errno;
        stat = ENETPLC5_E_UNIX_ERROR;
        showInfo( stat, __FILE__, __LINE__ );
        goto errReturn;
      }
    }

    n = write( ptr->sockfd, &buf[*numSent], l );

    if ( n == 0 ) {
      stat = ENETPLC5_E_CON_CLOSED;
      showInfo( stat, __FILE__, __LINE__ );
      goto errReturn;
    }
    else if ( n < 0 ) {
      perror( "sendPlcData - write" );
      if ( netConnectError( errno ) ) {
        stat = ENETPLC5_E_CON_RESET;
        showInfo( stat, __FILE__, __LINE__ );
        goto errReturn;
      }
      else {
        ptr->unixErrCode = errno;
        stat = ENETPLC5_E_UNIX_ERROR;
        showInfo( stat, __FILE__, __LINE__ );
        goto errReturn;
      }
    }

    ornlPLC5IncMsgsSent();

    *numSent += n;
    l -= n;

  }

  return ENETPLC5_E_SUCCESS;

errReturn:

  return stat;

}

static int getPlcData (
  privEnetPlc5CommPtr ptr,
  char *buf,
  int maxLen,
  int numBytesExpected,
  int statusOffset,
  int numBytesExpectedOnError,
  int numAdditionalBytesOnRemoteError,
  int *numRcvd
) {

  /* Read data from plc. If operation is successful, the expected */
  /* message length in bytes is <numBytesExpected>. If we get at */
  /* least <statusOffset> bytes, we may check the status (which is */
  /* always a single byte and a value of 0 = success. If we don't */
  /* receive <numBytesExpected> before some elapsed time, then */
  /* return a timeout error. */

fd_set fds;
int fd, l, n, numToRead, stat;
time_t start, stop;

  if ( ( maxLen < numBytesExpected ) || ( maxLen < statusOffset ) ) {
    stat = ENETPLC5_E_INVALID_PARAM;
    showInfo( stat, __FILE__, __LINE__ );
    goto errReturn;
  }

  *numRcvd = 0;
  l = maxLen;
  numToRead = numBytesExpected;
  while ( *numRcvd < numToRead ) {

    FD_ZERO( &fds );
    FD_SET( ptr->sockfd, &fds );

    start = time(NULL);
    
    /*fd = select( FD_SETSIZE, &fds, (fd_set *) NULL,*/
    /* (fd_set *) NULL, &ptr->timeout.timeval_time );*/
    {
    struct timeval timeout = { 2, 0 };
    fd = select( FD_SETSIZE, &fds, (fd_set *) NULL,
     (fd_set *) NULL, &timeout );
    }

    stop = time(NULL);
    
    if ( fd == 0 ) {
      ornlPLC5IncTimeouts();
      stat = ENETPLC5_E_TIMEOUT;
      showInfo( stat, __FILE__, __LINE__ );
      goto errReturn;
    }
    else if ( fd < 0 ) {
      if ( netConnectError( errno ) ) {
        stat = ENETPLC5_E_CON_RESET;
        showInfo( stat, __FILE__, __LINE__ );
        goto errReturn;
      }
      else {
        ptr->unixErrCode = errno;
        stat = ENETPLC5_E_UNIX_ERROR;
        showInfo( stat, __FILE__, __LINE__ );
        goto errReturn;
      }
    }

    start = time(NULL);

    n = read( ptr->sockfd, &buf[*numRcvd], l );

    stop = time(NULL);

    if ( n == 0 ) {
      stat = ENETPLC5_E_CON_CLOSED;
      showInfo( stat, __FILE__, __LINE__ );
      goto errReturn;
    }
    else if ( n < 0 ) {
      if ( netConnectError( errno ) ) {
        stat = ENETPLC5_E_CON_RESET;
        showInfo( stat, __FILE__, __LINE__ );
        goto errReturn;
      }
      else {
        ptr->unixErrCode = errno;
        stat = ENETPLC5_E_UNIX_ERROR;
        showInfo( stat, __FILE__, __LINE__ );
        goto errReturn;
      }
    }

    ornlPLC5IncMsgsRcvd();

    *numRcvd += n;
    l -= n;

    /* if we have the status, make sure it is 0. If status */
    /* is bad alter num of bytes to read */
    if ( *numRcvd >= statusOffset ) {
      if ( buf[statusOffset] != 0 ) {
        numToRead = numBytesExpectedOnError;
        if ( (unsigned char) buf[statusOffset] == 0xf0 ) { /* remote error */
          numToRead += numAdditionalBytesOnRemoteError;
	}
      }
    }

  }

  return ENETPLC5_E_SUCCESS;

errReturn:

  return stat;

}

static void trimWhiteSpace (
  char *str ) {

char buf[127+1];
int first, last, i, ii, l;

  l = strlen(str);
  if ( l > 126 ) l = 126;

  ii = 0;

  i = 0;
  while ( ( i < l ) && isspace( (unsigned char)str[i] ) ) {
    i++;
  }

  first = i;

  i = l-1;
  while ( ( i >= first ) && isspace( (unsigned char)str[i] ) ) {
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
        
      if ( isdigit((unsigned char)buf[i]) ) {
        i++;
        state = NUM;
        continue;
      }

      legal = 0;
      state = DONE;

      break;        

    case NUM:

      if ( isdigit((unsigned char)buf[i]) ) {
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

  l = strlen(string);

  if ( l < 1 ) return ENETPLC5_E_PLC_BAD_ADDR;

#if 0
  if ( l == 1 ) {
    *fileType = PLC5_K_BIN;
    if ( string[0] == 'O' )
      *fileNum = 0;
    else if ( string[0] == 'I' )
      *fileNum = 1;
    else
      return ENETPLC5_E_PLC_BAD_ADDR;
    return ENETPLC5_E_SUCCESS;
  }
#endif

  switch ( string[0] ) {

  case 'O':
    *fileType = PLC5_K_IO;
    *fileNum = 0;
    return ENETPLC5_E_SUCCESS;

  case 'I':
    *fileType = PLC5_K_IO;
    *fileNum = 1;
    return ENETPLC5_E_SUCCESS;

  case 'N':
    *fileType = PLC5_K_INT;
    break;

  case 'B':
    *fileType = PLC5_K_BIN;
    break;

  case 'T':
    *fileType = PLC5_K_TIMER;
    break;

  case 'S':
    *fileType = PLC5_K_STATUS;
    break;

  default:
    *fileType = 0;
    *fileNum = 0;
    return ENETPLC5_E_PLC_BAD_ADDR;

  }

  *fileNum = atol( &string[1] );
  if ( *fileNum == 0 ) return ENETPLC5_E_PLC_BAD_ADDR;

  return ENETPLC5_E_SUCCESS;

}

static int getOffset (
  char *string,
  int *offset
) {

  if ( !legalInt(string) ) return ENETPLC5_E_PLC_BAD_ADDR;

  *offset = atol( string );

  return ENETPLC5_E_SUCCESS;

}

static int getBit (
  char *string,
  int *bit
) {

  if ( !legalInt(string) ) return ENETPLC5_E_PLC_BAD_ADDR;

  *bit = atol( string );

  return ENETPLC5_E_SUCCESS;

}

static int getTimerMember (
  char *string,
  int *bit
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

  return ornlPLC5_Success;

}

static int parseAddress (
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
   * number (e.g. B3, N7, T4 )
   */


char buf[255], *buff, *tk, tmp[63+1];
int stat, state = GETTING_FILE_ID;

  strcpy( tmp, addr);
  
  strncpy( buf, addr, 255 );
  buf[254] = 0;

  tk = strtok_r( tmp, ":/", &buff );

  while ( state != DONE ) {

    switch ( state ) {

    case GETTING_FILE_ID:

      if ( !tk ) {
			printf("State = %d A", state);
			return ENETPLC5_E_PLC_BAD_ADDR;
		}

      /* get file type and file number */
      stat = getFileInfo( tk, fileType, fileNum );
      if ( !( stat & 1 ) ) {
			printf("State = %d B", state);
			return stat;
		}
		
      state = GETTING_OFFSET;

      break;

    case GETTING_OFFSET:

      if ( !tk ) {
			printf("State = %d A", state);
			return ENETPLC5_E_PLC_BAD_ADDR;
		}
      stat = getOffset( tk, offset );
		
      if ( !( stat & 1 ) ) {
			printf("State = %d B", state);
			return stat;
		}
      if ( *fileType == PLC5_K_BIN )
        state = GETTING_OPTIONAL_BIT;
      else if ( *fileType == PLC5_K_INT )
        state = GETTING_OPTIONAL_BIT;
      else if ( *fileType == PLC5_K_STATUS )
        state = GETTING_OPTIONAL_BIT;
      else if ( *fileType == PLC5_K_IO )
        state = GETTING_OPTIONAL_BIT;
      else if ( *fileType == PLC5_K_TIMER )
        state = GETTING_OPTIONAL_BIT;
      else {
        *bit = -1;
        state = DONE;
      }

      break;

    case GETTING_BIT:

      if ( !tk ) {
			printf("State = %d A", state);
			return ENETPLC5_E_PLC_BAD_ADDR;
		}

      stat = getBit( tk, bit );
		
      if ( !( stat & 1 ) ) {
			printf("State = %d B", state);
			return stat;
		}

      state = DONE;
      break;

    case GETTING_OPTIONAL_BIT:

      if ( !tk ) {

        *bit = -1;

      } else {
		
        if (*fileType == ornlPLC5_TIMER) { /* DBM */
          stat = getTimerMember( tk, (int *) bit );
        } else {
	        stat = getBit( tk, bit );
		  }
		  
        if ( !( stat & 1 ) ) {
			printf("State = %d ", state);
			return stat;
		  }
      }

      state = DONE;
      break;

    }

    tk = strtok_r( NULL, ":/", &buff );

  }

  return ENETPLC5_E_SUCCESS;

}

static int encodeDf1AddrOfs (
  privEnetPlc5CommPtr ptr,
  int fileNum,
  int offset,
  int *n,
  unsigned char *df1Addr
) {

unsigned char low, high;
/* DBM */
#if 0
if (fileNum == 4) {
printf( "encodeDf1AddrOfs\n" );
printf( "fileNum = %-d\n", fileNum );
printf( "offset = %-d\n", offset );
printf( "*n = %-d\n", *n );
}
#endif

  *n = 0;

	if (fileNum == 4) { /* Timers are special */
		df1Addr[*n] = 0xf; (*n)++; /* include file, offset and subelement in address */
		df1Addr[*n] = 0; (*n)++;
	} else {
	  df1Addr[*n] = 6; (*n)++; /* include file and offset in address */
	}
	
  /*df1Addr[*n] = 0; (*n)++; */

  if ( fileNum >= 0xff ) {
    low = fileNum & 0xff;
    high = ( fileNum >> 8 ) & 0xff;
    df1Addr[*n] = 0xff; (*n)++;
    df1Addr[*n] = low; (*n)++;
    df1Addr[*n] = high; (*n)++;
  }
  else {
    df1Addr[*n] = fileNum; (*n)++;
  }

  if ( offset >= 0xff ) {
    low = offset & 0xff;
    high = ( offset >> 8 ) & 0xff;
    df1Addr[*n] = 0xff; (*n)++;
    df1Addr[*n] = low; (*n)++;
    df1Addr[*n] = high; (*n)++;
  }
  else {
    df1Addr[*n] = offset; (*n)++;
  }

	if (fileNum == 4) { /* Timer again */
		df1Addr[*n] = 2; (*n)++;
	}	

  return ENETPLC5_E_SUCCESS;

}

static unsigned short getTns (
  privEnetPlc5CommPtr ptr
) {

  ptr->tns++;
  if ( ptr->tns == 0xffff ) ptr->tns = 2;

  return ptr->tns;

}

static void flushInput (
  privEnetPlc5CommPtr ptr
) {

int n, fd, more;
char buf[256];
fd_set fds;

  do {

    more = 0;

    FD_ZERO( &fds );
    FD_SET( ptr->sockfd, &fds );

    {
    struct timeval timeout = { 0, 0 };
    fd = select( FD_SETSIZE, &fds, (fd_set *) NULL,
     (fd_set *) NULL, &timeout );
    }

    if ( fd > 0 ) {
      n = read( ptr->sockfd, buf, 255 );
      if ( n > 0 ) more = 1;
    }

  } while ( more );

}

int enetPlc5GetUnixError (
  enetPlc5Comm handle
) {

privEnetPlc5CommPtr ptr = (privEnetPlc5CommPtr) handle;

  return ptr->unixErrCode;

}

/**
 * Translate Octal addresses (O: and I:) into decimal
 */

int transAddr(
  char *addr
) {

  char *p;
  int offVal = 0;
  int bitVal = -1;
  int i;

  if (strstr(addr, "O:") || strstr(addr, "I:")) {
    p = strchr(addr, ':');
	 if (p) {
	   p++;
      sscanf(p, "%o", &offVal);
    } else {
	   printf("transAddr can't find offset %s\n", addr);
		return 1;
	 }
    p = strchr(addr, '/');
	 if (p) {
	   p++;
      sscanf(p, "%o", &bitVal);
    } else {
	   printf("transAddr can't find bit %s\n", addr);
		return 1;
	 }
    p = strchr(addr, ':'); p++;
	 i = sprintf(p, "%d", offVal);
	 if (bitVal > -1) {
	 	p += i;
      i = sprintf(p, "/%d", bitVal);
    }
  }
  return 0;
}
  
void plc5WriteWord (
  char *ip,
  char *addr,
  char *word
) {

  enetPlc5Comm plc5;
  int retStat, stat;
  short words[8];
  char *p;

	printf("plc5WriteWord IP = %s Addr = %s Value = %s\n", ip, addr,word);
	

  stat = enetPlc5Init( &plc5 );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
    retStat = stat;
    goto errReturn;
  }

  enetPlc5SetTimeout( plc5, 10.0 );

  stat = enetPlc5ConnectPlc( plc5, ip );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
    retStat = stat;
    goto errReturn;
  }

  words[0] = (short) strtol(word, &p, 0);

  transAddr(addr);
  
  stat = enetPlc5WritePlc( plc5, addr, 1, words, NULL );
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

  return;
}

void plc5WriteBit (
  char *ip,
  char *addr,
  char *set
) {

enetPlc5Comm plc5;
int retStat, stat;

	printf("plc5WriteBit IP = %s Addr = %s Set = %s\n", ip, addr,set);
	

  stat = enetPlc5Init( &plc5 );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
    retStat = stat;
    goto errReturn;
  }

  enetPlc5SetTimeout( plc5, 10.0 );

  stat = enetPlc5ConnectPlc( plc5, ip );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
    retStat = stat;
    goto errReturn;
  }

  transAddr(addr);
  
  stat = enetPlc5WritePlcBit( plc5, addr, atoi(set), NULL );
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

  return;
}

void plc5ReadWord (
  char *ip,
  char *addr
) {

  enetPlc5Comm plc5;
  int retStat, stat;
  short words[8];

  stat = enetPlc5Init( &plc5 );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
    retStat = stat;
    goto errReturn;
  }

  enetPlc5SetTimeout( plc5, 10.0 );

  stat = enetPlc5ConnectPlc( plc5, ip );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
    retStat = stat;
    goto errReturn;
  }

  transAddr(addr);
  
  stat = enetPlc5ReadPlc( plc5, addr, 1, words, NULL );
  if ( !( stat & 1 ) ) {
    retStat = stat;
  }

  printf("plc5ReadWord IP = %s Addr = %s Value = 0x%x\n", ip, addr, words[0]);

  stat = enetPlc5DisconnectPlc( plc5 );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
  }

  stat = enetPlc5Destroy( &plc5 );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
  }

errReturn:

  return;
}

void plc5ReadBit (
  char *ip,
  char *addr
) {

  enetPlc5Comm plc5;
  int retStat, stat;
  int state;

  stat = enetPlc5Init( &plc5 );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Init\n", stat );
    retStat = stat;
    goto errReturn;
  }

  enetPlc5SetTimeout( plc5, 10.0 );

  stat = enetPlc5ConnectPlc( plc5, ip );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5ConnectPlc\n", stat );
    retStat = stat;
    goto errReturn;
  }

  transAddr(addr);

  stat = enetPlc5ReadPlcBit( plc5, addr, &state, NULL );
  if ( !( stat & 1 ) ) {
    retStat = stat;
  }

  printf("plc5ReadBit IP = %s Addr = %s State = %s\n", ip, addr, (state ? "True" : "False"));

  stat = enetPlc5DisconnectPlc( plc5 );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5DisconnectPlc\n", stat );
  }

  stat = enetPlc5Destroy( &plc5 );
  if ( !( stat & 1 ) ) {
    if ( plc5Msgs() ) printf( "Error %-d from enetPlc5Destroy\n", stat );
  }

errReturn:

  return;
}

void 	dumpWrite(
  unsigned char * buff, 
  int len
) {
  int i, j, k;
  
  j = k = 0;
  for (i = 0; i < len; i++) {
    printf("%02x ",buff[i]);
	 j++;
	 if (j == 8) {
	   printf(" ");
		j = 0;
		k++; 
	 }
	 if (k == 2) {
	 	printf("\n");
		k = 0;
	 }
  }
  printf("\n");
}

