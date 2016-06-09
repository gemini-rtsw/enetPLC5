#ifndef __enetPlc5Priv_linux_h
#define __enetPlc5Priv_linux_h 1

#include "enetPlc5.h"
#include "drvOrnlPLC5.h"

#define PLC5 1
#define PLC5250 2
#define SLC 3
#define MICRO 4

#define PCCC_VERSION 4
#define PCCC_BACKLOG 5
#define ETHERNET 1
#define INIT_CONNECT 1
#define MAX_CONNECT_TRIES 20 /* 5 */

#define DONE -1

#define SIGN_OR_NUM 1
#define NUM 2

#define GETTING_FILE_ID 1
#define GETTING_OFFSET 2
#define GETTING_BIT 3
#define GETTING_OPTIONAL_BIT 4

#define PLC5_K_INT 1
#define PLC5_K_BIN 2
#define PLC5_K_TIMER 3
#define PLC5_K_STATUS 4
#define PLC5_K_IO 5

/* ==================================================================== */

typedef struct customConnectInfoTag {
  short version;
  short backlog;
  unsigned char unused[12];
} customConnectInfoType, *customConnectInfoPtr;

static const int enetStatusOffset = 8; /* enough to know status */

typedef struct enetHdrTag {
  unsigned char mode;
  unsigned char subMode;
  unsigned short pccc_length;
  unsigned long conId;
  unsigned long status;
  customConnectInfoType custom;
} enetHdrType, *enetHdrPtr;

typedef struct enetMsgTag {
  enetHdrType hdr;
  unsigned char data[255];
} enetMsgType, *enetMsgPtr;

/* ==================================================================== */

#define rdModWrtReqEnetHdrSize 28
#define rdModWrtReqMsgHdrSize 9

typedef struct rdModWrtReqTag {
  /* enet hdr */
  unsigned char mode;
  unsigned char subMode;
  unsigned short pccc_length;
  unsigned long conId;
  unsigned long status;
  unsigned long request_id;
  unsigned long name_id;
  unsigned char unused[8];
  /* request msg hdr */
  unsigned char dst;
  unsigned char control;
  unsigned char src;
  unsigned char lsap;
  unsigned char cmd;
  unsigned char sts;
  unsigned short tns;
  unsigned char fnc;
  /* request msg data */
  unsigned char data[2048];
} rdModWrtReqType, *rdModWrtReqPtr;

#define rdModWrtReplyEnetHdrSize 28

/* don't include extSts */
#define rdModWrtReplyMsgHdrSize 8

/* enough to know status */
#define rdModWrtReplyStatusOffset 33

typedef struct rdModWrtReplyTag {
  /* enet hdr */
  unsigned char mode;
  unsigned char subMode;
  unsigned short pccc_length;
  unsigned long conId;
  unsigned long status;
  unsigned long request_id;
  unsigned long name_id;
  unsigned char unused[8];
  /* reply msg hdr */
  unsigned char src;
  unsigned char control;
  unsigned char dst;
  unsigned char lsap;
  unsigned char cmd;
  unsigned char sts;
  unsigned short tns;
  unsigned char extSts;
  unsigned char extra[240];
} rdModWrtReplyType, *rdModWrtReplyPtr;

/* ==================================================================== */

#define readBlockReqEnetHdrSize 28
#define readBlockReqMsgHdrSize 13

typedef struct readBlockReqTag {
  /* enet hdr */
  unsigned char mode;
  unsigned char subMode;
  unsigned short pccc_length;
  unsigned long conId;
  unsigned long status;
  unsigned long request_id;
  unsigned long name_id;
  unsigned char unused[8];
  /* request msg hdr */
  unsigned char dst;
  unsigned char control;
  unsigned char src;
  unsigned char lsap;
  unsigned char cmd;
  unsigned char sts;
  unsigned short tns;
  unsigned char fnc;
  unsigned char offset[2]; /*really an unsigned short but would be unaligned*/
  unsigned char trans[2];  /*really an unsigned short but would be unaligned*/
  /* request msg data */
  unsigned char data[2048];
} readBlockReqType, *readBlockReqPtr;

#define readBlockReplyEnetHdrSize 28
#define readBlockReplyMsgHdrSize 9

/* enough to know status */
#define readBlockReplyStatusOffset 33

typedef struct readBlockReplyTag {
  /* enet hdr */
  unsigned char mode;
  unsigned char subMode;
  unsigned short pccc_length;
  unsigned long conId;
  unsigned long status;
  unsigned long request_id;
  unsigned long name_id;
  unsigned char unused[8];
  /* reply msg hdr */
  unsigned char src;
  unsigned char control;
  unsigned char dst;
  unsigned char lsap;
  unsigned char cmd;
  unsigned char sts;
  unsigned short tns;
  unsigned char a;
  /* reply msg data */
  unsigned char bdata[2048];
} readBlockReplyType, *readBlockReplyPtr;

/* ==================================================================== */

#define wrtBlockReqEnetHdrSize 28
#define wrtBlockReqMsgHdrSize 13

typedef struct wrtBlockReqTag {
  /* enet hdr */
  unsigned char mode;
  unsigned char subMode;
  unsigned short pccc_length;
  unsigned long conId;
  unsigned long status;
  unsigned long request_id;
  unsigned long name_id;
  unsigned char unused[8];
  /* request msg hdr */
  unsigned char dst;
  unsigned char control;
  unsigned char src;
  unsigned char lsap;
  unsigned char cmd;
  unsigned char sts;
  unsigned short tns;
  unsigned char fnc;
  unsigned char offset[2]; /*really an unsigned short but would be unaligned*/
  unsigned char trans[2];  /*really an unsigned short but would be unaligned*/
  /* request msg data */
  unsigned char data[2048];
} wrtBlockReqType, *wrtBlockReqPtr;

#define wrtBlockReplyEnetHdrSize 28

/* don't include extSts */
#define wrtBlockReplyMsgHdrSize 8

/* enough to know status */
#define wrtBlockReplyStatusOffset 33

typedef struct wrtBlockReplyTag {
  /* enet hdr */
  unsigned char mode;
  unsigned char subMode;
  unsigned short pccc_length;
  unsigned long conId;
  unsigned long status;
  unsigned long request_id;
  unsigned long name_id;
  unsigned char unused[8];
  /* reply msg hdr */
  unsigned char src;
  unsigned char control;
  unsigned char dst;
  unsigned char lsap;
  unsigned char cmd;
  unsigned char sts;
  unsigned short tns;
  unsigned char extSts;
  unsigned char extra[240];
} wrtBlockReplyType, *wrtBlockReplyPtr;

/* ==================================================================== */

#define wrtBitReqEnetHdrSize 28
#define wrtBitReqMsgHdrSize 9

typedef struct wrtBitReqTag {
  /* enet hdr */
  unsigned char mode;
  unsigned char subMode;
  unsigned short pccc_length;
  unsigned long conId;
  unsigned long status;
  unsigned long request_id;
  unsigned long name_id;
  unsigned char unused[8];
  /* request msg hdr */
  unsigned char dst;
  unsigned char control;
  unsigned char src;
  unsigned char lsap;
  unsigned char cmd;
  unsigned char sts;
  unsigned short tns;
  unsigned char fnc;
  /* request msg data */
  unsigned char data[64];

} wrtBitReqType, *wrtBitReqPtr;

#define wrtBitReplyEnetHdrSize 28

/* don't include extSts */
#define wrtBitReplyMsgHdrSize 8

/* enough to know status */
#define wrtBitReplyStatusOffset 33

typedef struct wrtBitReplyTag {
  /* enet hdr */
  unsigned char mode;
  unsigned char subMode;
  unsigned short pccc_length;
  unsigned long conId;
  unsigned long status;
  unsigned long request_id;
  unsigned long name_id;
  unsigned char unused[8];
  /* reply msg hdr */
  unsigned char src;
  unsigned char control;
  unsigned char dst;
  unsigned char lsap;
  unsigned char cmd;
  unsigned char sts;
  unsigned short tns;
  unsigned char extSts;
} wrtBitReplyType, *wrtBitReplyPtr;

/* ==================================================================== */

typedef struct privEnetPlc5CommTag {
  int plcType;
  struct sockaddr_in sockAddr;
  int sockfd;
  unsigned long conId;      /* stored in network byte order */
  int connected;
  unsigned short tns;
  SYS_TIME_TYPE timeout;
  readBlockReqPtr readReq;  /* Only one (readReq or writeReq) allocated */
  int readReqLen;           /* bytes */
  int replyNumBytesExpected;
  int replyNumBytesExpectedOnError;
  wrtBlockReqPtr writeReq;  /* Only one (readReq or writeReq) allocated */
  int writeReqLen;          /* bytes */
  int writeReqDataIndex;
  wrtBitReqPtr writeBitReq;  /* Only one (readReq or writeReq) allocated */
  int writeBitReqLen;          /* bytes */
  int writeBitReqDataIndex;
  int unixErrCode;
} privEnetPlc5CommType, *privEnetPlc5CommPtr;

static int sendPlcData (
  privEnetPlc5CommPtr handle,
  char *buf,
  int num,
  int *numSent
);

static int getPlcData (
  privEnetPlc5CommPtr handle,
  char *buf,
  int maxLen,
  int numBytesExpected,
  int statusOffset,
  int numBytesExpectedOnError,
  int numAdditionalBytesOnRemoteError,
  int *numRcvd
);

static void trimWhiteSpace (
  char *str );

static int legalInt (
  char *str
);

static int getFileInfo (
  char *string,
  int *fileType,
  int *fileNum
);

static int getOffset (
  char *string,
  int *offset
);

static int getBit (
  char *string,
  int *bit
);

static int parseAddress (
  char *addr,
  int *fileType,
  int *fileNum,
  int *offset,
  int *bit
);

static int encodeDf1AddrOfs (
  privEnetPlc5CommPtr handle,
  int fileNum,
  int offset,
  int *n,
  unsigned char *df1Addr
);

static unsigned short getTns (
  privEnetPlc5CommPtr handle
);

static void flushInput (
  privEnetPlc5CommPtr handle
);

void 	dumpWrite(
  unsigned char * buff, 
  int len
);

#endif
