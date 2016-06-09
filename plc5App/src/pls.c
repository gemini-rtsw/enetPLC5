#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#ifdef vxWorks
#include <time.h>
#include <sys/times.h>
#include <taskLib.h>
#include <sockLib.h>
#include <selectLib.h>
#define FD_TABLE_SIZE FD_SETSIZE
#endif

#ifdef linux
#include <sys/time.h>
#define FD_TABLE_SIZE getdtablesize()
#endif

#if defined (__rtems__)
#include <strings.h>
#include <sys/select.h>
#define FD_TABLE_SIZE FD_SETSIZE 
#endif

#include "dbDefs.h"
#include "errlog.h"
#include "ellLib.h"
#include "ellLib.h"
#include "dbBase.h"
#include "dbStaticLib.h"
#include "link.h"
#include "dbFldTypes.h"
#include "recSup.h"
#include "devSup.h"
#include "drvSup.h"
#include "dbCommon.h"
#include "special.h"
#include "db_field_log.h"

#ifndef NO_EPICS
#include"epicsVersion.h"
#endif

#if EPICS_VERSION >= 3 && EPICS_REVISION >= 14

#define epicsExportSharedSymbols

#include "epicsThread.h"
#include "epicsMutex.h"
#include "dbAddr.h"
#include "dbLock.h"
#include "dbAccessDefs.h"
#include "dbTest.h"

#else

#include "dbAddr.h"
#include "dbLock.h"
#include "dbAccess.h"

extern struct dbBase *pdbbase;

#endif

#include "recGbl.h"
#include "dbEvent.h"
#include "callback.h"

#include "pls.version"
#include "ornl_avl.h"

#ifdef vxWorks

static char *strdup (
  char *src
) {

char *p;
int l = strlen(src);

  p = malloc( l + 1 );
  strncpy( p, src, l );
  p[l] = 0;

  return p;

}

#endif

#define MAXLEN 1400

typedef struct nameListTag {
  AVL_FIELDS(nameListTag)
  char *name;
} nameListType, *nameListPtr;

static int maxLen = MAXLEN;
static int num;
static int max;
static char **names;
static int *numPerSeg;
static int *bufLen;
static jmp_buf jump_h;
static int line = 0;
static AVL_HANDLE nameList;

static int compare_nodes (
  void *node1,
  void *node2
) {

nameListPtr p1, p2;

  p1 = (nameListPtr) node1;
  p2 = (nameListPtr) node2;

  return ( strcmp( p1->name, p2->name ) );

}

static int compare_key (
  void *key,
  void *node
) {

nameListPtr p;
char *one;

  p = (nameListPtr) node;
  one = (char *) key;

  return ( strcmp( one, p->name ) );

}

static int copy_node (
  void *node1,
  void *node2
) {

nameListPtr p1, p2;

  p1 = (nameListPtr) node1;
  p2 = (nameListPtr) node2;

  *p1 = *p2;

  return 1;

}

static void signal_handler (
  int sig
) {

  fprintf( stderr, "%s - got signal: sig = %-d\n", __FILE__, sig );
  longjmp( jump_h, 1 );

}

static long buildList ( void ) {

DBENTRY dbentry;
DBENTRY *pdbentry=&dbentry;
long status;

int i, last, avlStat, dup;
int len;
char tmp[15+1];
nameListPtr curNode;

  avlStat = avl_init_tree( compare_nodes, compare_key, copy_node, &nameList );
  if ( !( avlStat & 1 ) ) {
    return 0;
  }

  num = len = 0;
  max = 0;
  names = NULL;
  numPerSeg = NULL;
  bufLen = NULL;

  if(!pdbbase) {
    /* fprintf( stderr, "%s - no database has been loaded\n", __FILE__ ); */
    return 0;
  }

  /* Build list to sort names */

  dbInitEntry(pdbbase,pdbentry);

  status = dbFirstRecordType(pdbentry);

  if(status) {
    /* fprintf( stderr, "%s - no record description\n", __FILE__ ); */
    return 0;
  }

  while(!status) {

    status = dbFirstRecord(pdbentry);

    while(!status) {

      curNode = (nameListPtr) calloc( sizeof(nameListType), 1 );
      if ( !curNode ) return 0;
      curNode->name = strdup( dbGetRecordName( pdbentry ) );
      if ( !curNode->name ) return 0;

      avlStat = avl_insert_node( nameList, (void *) curNode, &dup );
      if ( !( avlStat & 1 ) ) {
	return 0;
      }
      if ( dup ) {
        fprintf( stderr, "%s - duplicate pv name\n", __FILE__ );
        free( curNode->name );
        free( curNode );
      }

      status = dbNextRecord(pdbentry);

    }

    status = dbNextRecordType(pdbentry);

  }

  dbFinishEntry(pdbentry);

  /* get num pvs and max */

  max = 1;

  avlStat = avl_get_first( nameList, (void **) &curNode );
  if ( !( avlStat & 1 ) ) return 0;
  while ( curNode ) {

    num++;
    len += strlen( curNode->name ) + 1;
    if ( len > maxLen ) {
      max++;
      len = strlen( curNode->name ) + 1;
    }

    avlStat = avl_get_next( nameList, (void **) &curNode );
    if ( !( avlStat & 1 ) ) return 0;

  }

  names = (char **) calloc( sizeof(char **), max );
  numPerSeg = (int *) calloc( sizeof(int), max );
  bufLen = (int *) calloc( sizeof(int), max );

  for ( i=0; i<max; i++ ) {
    numPerSeg[i] = 0;
  }

  /* get string lengths */

  len = 0;
  last = max = 1;

  avlStat = avl_get_first( nameList, (void **) &curNode );
  if ( !( avlStat & 1 ) ) return 0;
  while ( curNode ) {

    len += strlen( curNode->name ) + 1;

    if ( len > maxLen ) {
      max++;
      len = strlen( curNode->name ) + 1;
    }

    bufLen[max-1] = len;
    numPerSeg[max-1]++;

    avlStat = avl_get_next( nameList, (void **) &curNode );
    if ( !( avlStat & 1 ) ) return 0;

  }

  /* allocate name buffers */
  for ( i=0; i<max; i++ ) {
    sprintf( tmp, "%-d", numPerSeg[i] );
    bufLen[i] += 5 + strlen(tmp);
    names[i] = (char *) calloc( sizeof(char), bufLen[i]+10 );
    sprintf( names[i], "ok %s ", tmp );
    /* printf( "bufLen[%-d] = %-d\n", i, bufLen[i] ); */
  }

  /* get names */

  len = 0;
  max = 1;

  avlStat = avl_get_first( nameList, (void **) &curNode );
  if ( !( avlStat & 1 ) ) return 0;
  while ( curNode ) {

    len += strlen( curNode->name ) + 1;
    if ( len > maxLen ) {
      max++;
      len = strlen( curNode->name ) + 1;
    }

    strcat( names[max-1], curNode->name );
    strcat( names[max-1], " " );

    avlStat = avl_get_next( nameList, (void **) &curNode );
    if ( !( avlStat & 1 ) ) return 0;

  }

  /* delete tree */
  avlStat = avl_get_first( nameList, (void **) &curNode );
  if ( !( avlStat & 1 ) ) return 0;
  while ( curNode ) {

    avlStat = avl_delete_node( nameList, (void **) &curNode );
    if ( !( avlStat & 1 ) ) return 0;

    if ( curNode->name ) {
      free( curNode->name );
      curNode->name = NULL;
    }

    free( curNode );
    curNode = NULL;

    avlStat = avl_get_first( nameList, (void **) &curNode );
    if ( !( avlStat & 1 ) ) return 0;

  }

  avlStat = avl_destroy( nameList );
  if ( !( avlStat & 1 ) ) return 0;
  nameList = NULL;

  return 0;

}

static int reply (
  int socketFd,
  char *msg
) {

struct timeval timeout;
int more, fd, i, remain, len;
fd_set fds;

  timeout.tv_sec = 10;
  timeout.tv_usec = 0;

  more = 1;
  i = 0;
  remain = strlen(msg);
  while ( more ) {

    FD_ZERO( &fds );
    FD_SET( socketFd, &fds );

    fd = select( FD_TABLE_SIZE, (fd_set *) NULL, &fds,
     (fd_set *) NULL, &timeout );

    if ( fd == 0 ) { /* timeout */
      /* printf( "timeout\n" ); */
      return 0;
    }

    if ( fd < 0 ) { /* error */
      perror( "select" );
      return 0;
    }

    len = write( socketFd, &msg[i], remain );
    if ( len < 1 ) return len; /* error */

    remain -= len;
    i += len;

    if ( remain < 1 ) more = 0;

  } while ( more );

  return i;

}

static int getCommand (
  int socketFd,
  char *msg,
  int maxLen
) {

struct timeval timeout;
int more, i, ii, remain, len, count, n;
fd_set fds;

  timeout.tv_sec = 3;
  timeout.tv_usec = 0;

  more = 1;
  i = count = 0;
  remain = maxLen;
  while ( more ) {

    /* printf( "socketFd = %-d\n", socketFd ); */

    FD_ZERO( &fds );
    FD_SET( socketFd, &fds );

    n = select( FD_TABLE_SIZE, &fds, (fd_set *) NULL,
     (fd_set *) NULL, &timeout );

    if ( n == 0 ) { /* timeout */
      return 0;
    }
    if ( n < 0 ) { /* error */
      perror( "select" );
      return n;
    }

    strcpy( msg, "" );

    len = read( socketFd, &msg[i], remain );
    /* printf( "len = %-d\n", len ); */
    if ( len < 1 ) return len; /* error */

    for ( ii=0; ii<len; ii++ ) {
      if ( msg[i+ii] == '\n' ) {
        msg[i+ii] = 0;
        len = strlen(msg);
        more = 0;
        break;
      }
    }

    if ( more ) {

      remain -= len;
      i += len;

      if ( remain <= 0 ) return 0;

    }

  } while ( more );

  return len;

}

static void func (
  void *arg
) {

int *port = (int *) arg;
struct sockaddr_in s, cli_s;
int sockfd, newsockfd, more, run;
int stat;
unsigned short port_num, cliPort;
int value, len,  group, n;
socklen_t cliLen;
char msg[MAXLEN+11], cmd[31+1], parm[31+1], *tk, *ctx;
struct sigaction sa, oldsa, dummysa;
int socketOpened=0, newSocketOpened=0;

  stat = buildList();

  if ( arg == NULL ) {
    fprintf( stderr, "%s - port missing\n", __FILE__ );
    return;
  }

  socketOpened = 0;
  newSocketOpened = 0;

  stat = setjmp( jump_h );
  if ( !stat ) {

    sa.sa_handler = signal_handler;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = 0;

    stat = sigaction( SIGILL, &sa, &oldsa );
    stat = sigaction( SIGSEGV, &sa, &dummysa );
    stat = sigaction( SIGPIPE, &sa, &dummysa );

  }
  else {

    fprintf( stderr, "%s - exception! Last line: %d\n", __FILE__, line );

#ifdef vxWorks
    taskDelay(3*60);
#endif

#ifdef linux
    sleep(3);
#endif

    if ( socketOpened ) {
      stat = close( sockfd );
      socketOpened = 0;
    }

    if ( newSocketOpened ) {
      stat = close( newsockfd );
      newSocketOpened = 0;
    }

  }

  run = 1;
  while ( run ) {

    line = __LINE__;

    sockfd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if ( sockfd == -1 ) {
      perror( "socket" );
      return;
    }

    socketOpened = 1;

    line = __LINE__;

    value = 1;
    len = sizeof(value);

    stat = setsockopt( sockfd, SOL_SOCKET, SO_REUSEADDR,
     (char *) &value, len );

#ifdef linux
    stat = setsockopt( sockfd, SOL_SOCKET, SO_REUSEADDR,
     &value, len );
#endif

    if ( sockfd == -1 ) {
      perror( "setsockopt" );
      return;
    }

    port_num = (unsigned short) *port;

    port_num = htons( port_num );

    line = __LINE__;

    bzero( (char *) &s, sizeof(s) );
    s.sin_family = AF_INET;
    s.sin_addr.s_addr = htonl(INADDR_ANY);
    s.sin_port = port_num;

    line = __LINE__;

    /* do bind and listen */
    stat = bind( sockfd, (struct sockaddr*) &s, sizeof(s) );
    if ( stat < 0 ) {
      perror( "bind" );
      return;
    }

    line = __LINE__;

    stat = listen( sockfd, 5 );
    if ( stat < 0 ) {
      perror( "listen" );
      return;
    }

    more = 1;
    while ( more ) {

      line = __LINE__;

      /* printf( "sockfd = %-d\n", sockfd ); */

      /* accept connection */
      cliLen = sizeof(cli_s);
      newsockfd = accept( sockfd, (struct sockaddr *) &cli_s,
       &cliLen );

      line = __LINE__;

      if ( newsockfd < 0 ) {
        perror( "accept" );
        return;
      }

      newSocketOpened = 1;

      line = __LINE__;

      cliPort = ntohs( cli_s.sin_port);

      /* printf( "connected, client port = %-d\n", (int) cliPort ); */

      line = __LINE__;

#ifdef linux
      value = 1;
      len = sizeof(value);
      stat = setsockopt( newsockfd, IPPROTO_TCP, TCP_NODELAY, &value, len );
#endif

      line = __LINE__;

      n = getCommand( newsockfd, msg, 16 );
      /* printf( "msg = [%s]\n", msg ); */

      /* printf( "n = %-d\n", n ); */
      if ( n ) {

        line = __LINE__;

        ctx = NULL;
        tk = strtok_r( msg, " ", &ctx );
        if ( tk ) {

          line = __LINE__;

          strncpy( cmd, tk, 31 );
          cmd[31] = 0;

          line = __LINE__;

          tk = strtok_r( NULL, " ", &ctx );
          if ( tk ) {

            line = __LINE__;

            strncpy( parm, tk, 31 );
            parm[31] = 0;

          }
          else {

            line = __LINE__;

            strcpy( parm, "" );

          }

        }
        else {

          line = __LINE__;

          strcpy( cmd, "" );

        }

        line = __LINE__;

	/* reply */
        if ( strcmp( cmd, "numpvs" ) == 0 ) {

          line = __LINE__;

          sprintf( msg, "ok %-d\n", num );
          stat = reply( newsockfd, msg );

        }
        else if ( strcmp( cmd, "bufsize" ) == 0 ) {

          line = __LINE__;

          sprintf( msg, "ok %-d\n", maxLen+10 );
          stat = reply( newsockfd, msg );

        }
        else if ( strcmp( cmd, "getpvs" ) == 0 ) {

          line = __LINE__;

          group = atol( parm );
          if ( ( group < 0 ) || ( group >= max ) ) {
            line = __LINE__;
            stat = reply( newsockfd, "nomore\n" );
          }
          else {
            line = __LINE__;
            strcpy( msg, names[group] );
            strcat( msg, "\n" );
            stat = reply( newsockfd, msg );
          }

        }
        else if ( strcmp( cmd, "kill" ) == 0 ) {

          line = __LINE__;

          stat = reply( newsockfd, "ok\n" );
          more = 0;
          run = 0;

        }
        else {

          line = __LINE__;

          stat = reply( newsockfd, "unknown\n" );

        }

      }
      else {

	line = __LINE__;

	fprintf( stderr, "%s - no cmd received\n", __FILE__ );

      }

/***
* disconnect asychronously
*/
      line = __LINE__;

      stat = shutdown( newsockfd, 2 );

      line = __LINE__;

      stat = close( newsockfd );

    }

    line = __LINE__;

    stat = shutdown( sockfd, 2 );
    stat = close( sockfd );

  }

  line = __LINE__;

  return;

}

long epicsShareAPI pvlistserver (
  char *arg0
) {

#if EPICS_VERSION >= 3 && EPICS_REVISION >= 14

epicsThreadId id;
static int port;

  if ( arg0 == NULL ) {
    fprintf( stderr, "pvlistserver - version %s\n", VERSION );
    fprintf( stderr, "usage: pvlistserver(\"<port>\")\n" );
    return 0;
  }

  port = atol( arg0 );

  id = epicsThreadCreate( "pvlistserver",
   epicsThreadPriorityLow,
   epicsThreadGetStackSize( epicsThreadStackMedium ),
   func, (void *) &port );

#else

static int port;
int id, prior, stack_size;

  if ( arg0 == NULL ) {
    fprintf( stderr, "pvlistserver - version %s\n", VERSION );
    fprintf( stderr, "usage: pvlistserver(\"<port>\")\n" );
    return 0;
  }

  port = atol( arg0 );

  prior = 128;
  stack_size = 8192;
  id = taskSpawn( "pvlistserver", prior, VX_FP_TASK, stack_size,
   (FUNCPTR) func, (int) &port, NULL, NULL, NULL, NULL, NULL,
   NULL, NULL, NULL, NULL );

#endif

  return 0;

}
