#undef __INCLUDE_MAIN__

#include "devManager.h"

typedef struct numListTag {
  struct numListTag *flink;
  int i0;
  int i1;
} numListType, *numListPtr;

static numListPtr g_memHead, g_memTail;
static numListPtr g_ioHead, g_ioTail;
static numListPtr g_intHead, g_intTail;
static numListPtr g_dmaHead, g_dmaTail;
static numListPtr g_busHead, g_busTail;

static int g_major = 1, g_minor = 0, g_release = 0;
static int g_init_done = 0;
static AVL_HANDLE g_devices;
static char g_buf[511+1], *g_tk;
static int g_need_to_read_file = 1;
static int g_line = 0;
static devListPtr g_curDev = NULL;

static AVL_HANDLE g_devices;
static devListPtr g_curDevFromAll = NULL;
static int g_getNextFromAll = 0;

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

static int compare_dev_nodes (
  void *node1,
  void *node2
) {

devListPtr p1, p2;

  p1 = (devListPtr) node1;
  p2 = (devListPtr) node2;

  return ( strcmp( p1->name, p2->name ) );

}

static int compare_dev_key (
  void *key,
  void *node
) {

devListPtr p;
char *one;

  p = (devListPtr) node;
  one = (char *) key;

  return ( strcmp( one, p->name ) );

}

static int copy_dev_nodes (
  void *node1,
  void *node2
) {

devListPtr p1, p2;

  p1 = (devListPtr) node1;
  p2 = (devListPtr) node2;

  *p1 = *p2;

  return 1;

}

static int compare_prop_nodes (
  void *node1,
  void *node2
) {

propListPtr p1, p2;
int u1, u2;

  p1 = (propListPtr) node1;
  u1 = atol( p1->unit );

  p2 = (propListPtr) node2;
  u2 = atol( p2->unit );

  if ( u1 > u2 ) {
    return 1;
  }
  else if ( u1 < u2 ) {
    return -1;
  }
  else {
    return 0;
  }

  return ( strcmp( p1->unit, p2->unit ) );

}

static int compare_prop_key (
  void *key,
  void *node
) {

propListPtr p;
char *one;
int u1, u2;

  one = (char *) key;
  u1 = atol( one );

  p = (propListPtr) node;
  u2 = atol( p->unit );

  if ( u1 > u2 ) {
    return 1;
  }
  else if ( u1 < u2 ) {
    return -1;
  }
  else {
    return 0;
  }

}

static int copy_prop_nodes (
  void *node1,
  void *node2
) {

propListPtr p1, p2;

  p1 = (propListPtr) node1;
  p2 = (propListPtr) node2;

  *p1 = *p2;

  return 1;

}

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

      g_tk = strtok( g_buf, " \t\n" );
      if ( !g_tk ) g_need_to_read_file = 1;

    }
    else {

      g_tk = strtok( NULL, " \t\n" );
      if ( !g_tk ) {
        /* printf( "\n" ); */
        g_need_to_read_file = 1;
      }

    }

  } while ( !g_tk );

  return ERR_OK;

}

static void decodeKeyword (
  char *devType,
  char *deviceKeyword,
  char *genericKeyword,
  int max
) {

  strncpy( genericKeyword, deviceKeyword, max );
  genericKeyword[max] = 0;

  if ( strcmp( devType, "serial" ) == 0 ) {

    if ( strcmp( deviceKeyword, "baud" ) == 0 ) {
      strncpy( genericKeyword, "prop0", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "data_bits" ) == 0 ) {
      strncpy( genericKeyword, "prop1", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "stop_bits" ) == 0 ) {
      strncpy( genericKeyword, "prop2", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "parity" ) == 0 ) {
      strncpy( genericKeyword, "prop3", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "ip_addr" ) == 0 ) {
      strncpy( genericKeyword, "prop6", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "ip_port" ) == 0 ) {
      strncpy( genericKeyword, "prop7", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "ip_proto" ) == 0 ) {
      strncpy( genericKeyword, "prop8", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "dev_addr" ) == 0 ) {
      strncpy( genericKeyword, "prop9", max );
      genericKeyword[max] = 0;
    }

  }
  else if ( strcmp( devType, "df1" ) == 0 ) {

    if ( strcmp( deviceKeyword, "baud" ) == 0 ) {
      strncpy( genericKeyword, "prop0", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "data_bits" ) == 0 ) {
      strncpy( genericKeyword, "prop1", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "stop_bits" ) == 0 ) {
      strncpy( genericKeyword, "prop2", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "parity" ) == 0 ) {
      strncpy( genericKeyword, "prop3", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "df1_addr" ) == 0 ) {
      strncpy( genericKeyword, "prop4", max );
      genericKeyword[max] = 0;
    }

  }
  else if ( strcmp( devType, "plc5Enet" ) == 0 ) {

    if ( strcmp( deviceKeyword, "ip_addr" ) == 0 ) {
      strncpy( genericKeyword, "prop0", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "port" ) == 0 ) {
      strncpy( genericKeyword, "prop1", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "plc5_tbl_addr" ) == 0 ) {
      strncpy( genericKeyword, "prop2", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "plc5_tbl_len" ) == 0 ) {
      strncpy( genericKeyword, "prop3", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "scan_rate" ) == 0 ) {
      strncpy( genericKeyword, "prop4", max );
      genericKeyword[max] = 0;
    }
    if ( strcmp( deviceKeyword, "direction" ) == 0 ) {
      strncpy( genericKeyword, "prop5", max );
      genericKeyword[max] = 0;
    }

  }

}

static int numeric (
  char *number
) {

int value;
char *bad;

  value = strtol( number, &bad, 0 );

  if ( *bad ) return 0;

  return 1;

}

static int setKeyword (
  char *devType,
  propListPtr prop,
  char *deviceKeyword,
  char *value
) {

char keyword[63+1];

  decodeKeyword( devType, deviceKeyword, keyword, 63 );

  if ( strcmp( keyword, "device_name" ) == 0 ) {

    prop->deviceName = (char *) malloc( strlen(value)+1 );
    if ( !prop->deviceName ) return ERR_NOMEM;
    strcpy( prop->deviceName, value );

  }

  else if ( strcmp( keyword, "prop0" ) == 0 ) {

    prop->prop0.name = (char *) malloc( strlen(deviceKeyword)+1 );
    if ( !prop->prop0.name ) return ERR_NOMEM;
    strcpy( prop->prop0.name, deviceKeyword );

    prop->prop0.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->prop0.value ) return ERR_NOMEM;
    strcpy( prop->prop0.value, value );

  }
  else if ( strcmp( keyword, "prop1" ) == 0 ) {

    prop->prop1.name = (char *) malloc( strlen(deviceKeyword)+1 );
    if ( !prop->prop1.name ) return ERR_NOMEM;
    strcpy( prop->prop1.name, deviceKeyword );

    prop->prop1.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->prop1.value ) return ERR_NOMEM;
    strcpy( prop->prop1.value, value );

  }
  else if ( strcmp( keyword, "prop2" ) == 0 ) {

    prop->prop2.name = (char *) malloc( strlen(deviceKeyword)+1 );
    if ( !prop->prop2.name ) return ERR_NOMEM;
    strcpy( prop->prop2.name, deviceKeyword );

    prop->prop2.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->prop2.value ) return ERR_NOMEM;
    strcpy( prop->prop2.value, value );

  }
  else if ( strcmp( keyword, "prop3" ) == 0 ) {

    prop->prop3.name = (char *) malloc( strlen(deviceKeyword)+1 );
    if ( !prop->prop3.name ) return ERR_NOMEM;
    strcpy( prop->prop3.name, deviceKeyword );

    prop->prop3.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->prop3.value ) return ERR_NOMEM;
    strcpy( prop->prop3.value, value );

  }
  else if ( strcmp( keyword, "prop4" ) == 0 ) {

    prop->prop4.name = (char *) malloc( strlen(deviceKeyword)+1 );
    if ( !prop->prop4.name ) return ERR_NOMEM;
    strcpy( prop->prop4.name, deviceKeyword );

    prop->prop4.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->prop4.value ) return ERR_NOMEM;
    strcpy( prop->prop4.value, value );

  }
  else if ( strcmp( keyword, "prop5" ) == 0 ) {

    prop->prop5.name = (char *) malloc( strlen(deviceKeyword)+1 );
    if ( !prop->prop5.name ) return ERR_NOMEM;
    strcpy( prop->prop5.name, deviceKeyword );

    prop->prop5.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->prop5.value ) return ERR_NOMEM;
    strcpy( prop->prop5.value, value );

  }
  else if ( strcmp( keyword, "prop6" ) == 0 ) {

    prop->prop6.name = (char *) malloc( strlen(deviceKeyword)+1 );
    if ( !prop->prop6.name ) return ERR_NOMEM;
    strcpy( prop->prop6.name, deviceKeyword );

    prop->prop6.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->prop6.value ) return ERR_NOMEM;
    strcpy( prop->prop6.value, value );

  }
  else if ( strcmp( keyword, "prop7" ) == 0 ) {

    prop->prop7.name = (char *) malloc( strlen(deviceKeyword)+1 );
    if ( !prop->prop7.name ) return ERR_NOMEM;
    strcpy( prop->prop7.name, deviceKeyword );

    prop->prop7.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->prop7.value ) return ERR_NOMEM;
    strcpy( prop->prop7.value, value );

  }
  else if ( strcmp( keyword, "prop8" ) == 0 ) {

    prop->prop8.name = (char *) malloc( strlen(deviceKeyword)+1 );
    if ( !prop->prop8.name ) return ERR_NOMEM;
    strcpy( prop->prop8.name, deviceKeyword );

    prop->prop8.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->prop8.value ) return ERR_NOMEM;
    strcpy( prop->prop8.value, value );

  }
  else if ( strcmp( keyword, "prop9" ) == 0 ) {

    prop->prop9.name = (char *) malloc( strlen(deviceKeyword)+1 );
    if ( !prop->prop9.name ) return ERR_NOMEM;
    strcpy( prop->prop9.name, deviceKeyword );

    prop->prop9.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->prop9.value ) return ERR_NOMEM;
    strcpy( prop->prop9.value, value );

  }

  else if ( strcmp( keyword, "mem" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->mem0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->mem0.name ) return ERR_NOMEM;
    strcpy( prop->mem0.name, keyword );

    prop->mem0.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->mem0.value ) return ERR_NOMEM;
    strcpy( prop->mem0.value, value );

  }
  else if ( strcmp( keyword, "mem0" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->mem0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->mem0.name ) return ERR_NOMEM;
    strcpy( prop->mem0.name, keyword );

    prop->mem0.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->mem0.value ) return ERR_NOMEM;
    strcpy( prop->mem0.value, value );

  }
  else if ( strcmp( keyword, "mem1" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->mem1.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->mem1.name ) return ERR_NOMEM;
    strcpy( prop->mem1.name, keyword );

    prop->mem1.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->mem1.value ) return ERR_NOMEM;
    strcpy( prop->mem1.value, value );

  }
  else if ( strcmp( keyword, "mem2" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->mem2.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->mem2.name ) return ERR_NOMEM;
    strcpy( prop->mem2.name, keyword );

    prop->mem2.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->mem2.value ) return ERR_NOMEM;
    strcpy( prop->mem2.value, value );

  }

  else if ( strcmp( keyword, "io" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->io0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->io0.name ) return ERR_NOMEM;
    strcpy( prop->io0.name, keyword );

    prop->io0.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->io0.value ) return ERR_NOMEM;
    strcpy( prop->io0.value, value );

  }
  else if ( strcmp( keyword, "io0" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->io0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->io0.name ) return ERR_NOMEM;
    strcpy( prop->io0.name, keyword );

    prop->io0.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->io0.value ) return ERR_NOMEM;
    strcpy( prop->io0.value, value );

  }
  else if ( strcmp( keyword, "io1" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->io1.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->io1.name ) return ERR_NOMEM;
    strcpy( prop->io1.name, keyword );

    prop->io1.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->io1.value ) return ERR_NOMEM;
    strcpy( prop->io1.value, value );

  }
  else if ( strcmp( keyword, "io2" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->io2.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->io2.name ) return ERR_NOMEM;
    strcpy( prop->io2.name, keyword );

    prop->io2.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->io2.value ) return ERR_NOMEM;
    strcpy( prop->io2.value, value );

  }

  else if ( strcmp( keyword, "int" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->int0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->int0.name ) return ERR_NOMEM;
    strcpy( prop->int0.name, keyword );

    prop->int0.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->int0.value ) return ERR_NOMEM;
    strcpy( prop->int0.value, value );

  }
  else if ( strcmp( keyword, "int0" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->int0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->int0.name ) return ERR_NOMEM;
    strcpy( prop->int0.name, keyword );

    prop->int0.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->int0.value ) return ERR_NOMEM;
    strcpy( prop->int0.value, value );

  }
  else if ( strcmp( keyword, "int1" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->int1.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->int1.name ) return ERR_NOMEM;
    strcpy( prop->int1.name, keyword );

    prop->int1.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->int1.value ) return ERR_NOMEM;
    strcpy( prop->int1.value, value );

  }
  else if ( strcmp( keyword, "int2" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->int2.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->int2.name ) return ERR_NOMEM;
    strcpy( prop->int2.name, keyword );

    prop->int2.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->int2.value ) return ERR_NOMEM;
    strcpy( prop->int2.value, value );

  }

  else if ( strcmp( keyword, "dma" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->dma0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->dma0.name ) return ERR_NOMEM;
    strcpy( prop->dma0.name, keyword );

    prop->dma0.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->dma0.value ) return ERR_NOMEM;
    strcpy( prop->dma0.value, value );

  }
  else if ( strcmp( keyword, "dma0" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->dma0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->dma0.name ) return ERR_NOMEM;
    strcpy( prop->dma0.name, keyword );

    prop->dma0.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->dma0.value ) return ERR_NOMEM;
    strcpy( prop->dma0.value, value );

  }
  else if ( strcmp( keyword, "dma1" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->dma1.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->dma1.name ) return ERR_NOMEM;
    strcpy( prop->dma1.name, keyword );

    prop->dma1.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->dma1.value ) return ERR_NOMEM;
    strcpy( prop->dma1.value, value );

  }
  else if ( strcmp( keyword, "dma2" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->dma2.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->dma2.name ) return ERR_NOMEM;
    strcpy( prop->dma2.name, keyword );

    prop->dma2.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->dma2.value ) return ERR_NOMEM;
    strcpy( prop->dma2.value, value );

  }

  else if ( strcmp( keyword, "bus" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->bus.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->bus.name ) return ERR_NOMEM;
    strcpy( prop->bus.name, keyword );

    prop->bus.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->bus.value ) return ERR_NOMEM;
    strcpy( prop->bus.value, value );

  }
  else if ( strcmp( keyword, "node" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->node.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->node.name ) return ERR_NOMEM;
    strcpy( prop->node.name, keyword );

    prop->node.value = (char *) malloc( strlen(value)+1 );
    if ( !prop->node.value ) return ERR_NOMEM;
    strcpy( prop->node.value, value );

  }

  else {

    printf( "setKeyword, Unknown keyword: [%s]\n", keyword );
    return ERR_UNKNOWN_KEYWORD;

  }

  return ERR_OK;

}

static int setKeyword2 (
  char *devType,
  propListPtr prop,
  char *keyword,
  char *value
) {

  if ( strcmp( keyword, "mem" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->mem0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->mem0.name ) return ERR_NOMEM;
    strcpy( prop->mem0.name, keyword );

    prop->mem0.value2 = (char *) malloc( strlen(value)+1 );
    if ( !prop->mem0.value2 ) return ERR_NOMEM;
    strcpy( prop->mem0.value2, value );

  }
  else if ( strcmp( keyword, "mem0" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->mem0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->mem0.name ) return ERR_NOMEM;
    strcpy( prop->mem0.name, keyword );

    prop->mem0.value2 = (char *) malloc( strlen(value)+1 );
    if ( !prop->mem0.value2 ) return ERR_NOMEM;
    strcpy( prop->mem0.value2, value );

  }
  else if ( strcmp( keyword, "mem1" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->mem1.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->mem1.name ) return ERR_NOMEM;
    strcpy( prop->mem1.name, keyword );

    prop->mem1.value2 = (char *) malloc( strlen(value)+1 );
    if ( !prop->mem1.value2 ) return ERR_NOMEM;
    strcpy( prop->mem1.value2, value );

  }
  else if ( strcmp( keyword, "mem2" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->mem2.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->mem2.name ) return ERR_NOMEM;
    strcpy( prop->mem2.name, keyword );

    prop->mem2.value2 = (char *) malloc( strlen(value)+1 );
    if ( !prop->mem2.value2 ) return ERR_NOMEM;
    strcpy( prop->mem2.value2, value );

  }

  else if ( strcmp( keyword, "io" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->io0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->io0.name ) return ERR_NOMEM;
    strcpy( prop->io0.name, keyword );

    prop->io0.value2 = (char *) malloc( strlen(value)+1 );
    if ( !prop->io0.value2 ) return ERR_NOMEM;
    strcpy( prop->io0.value2, value );

  }
  else if ( strcmp( keyword, "io0" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->io0.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->io0.name ) return ERR_NOMEM;
    strcpy( prop->io0.name, keyword );

    prop->io0.value2 = (char *) malloc( strlen(value)+1 );
    if ( !prop->io0.value2 ) return ERR_NOMEM;
    strcpy( prop->io0.value2, value );

  }
  else if ( strcmp( keyword, "io1" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->io1.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->io1.name ) return ERR_NOMEM;
    strcpy( prop->io1.name, keyword );

    prop->io1.value2 = (char *) malloc( strlen(value)+1 );
    if ( !prop->io1.value2 ) return ERR_NOMEM;
    strcpy( prop->io1.value2, value );

  }
  else if ( strcmp( keyword, "io2" ) == 0 ) {

    if ( !numeric(value) ) return ERR_SYNTAX;

    prop->io2.name = (char *) malloc( strlen(keyword)+1 );
    if ( !prop->io2.name ) return ERR_NOMEM;
    strcpy( prop->io2.name, keyword );

    prop->io2.value2 = (char *) malloc( strlen(value)+1 );
    if ( !prop->io2.value2 ) return ERR_NOMEM;
    strcpy( prop->io2.value2, value );

  }

  else {

    printf( "setKeyword2, Unknown keyword: [%s]\n", keyword );
    return ERR_UNKNOWN_KEYWORD;

  }

  return ERR_OK;

}

static int buildListFromFile (
  FILE *inFile
) {

int stat, state, dup;
devListPtr curDev = NULL;
propListPtr curProp = NULL;
char devType[31+1], devName[63+1], devUnit[7+1], keyword[63+1],
 value[63+1];

/* printf( "buildListFromFile\n" ); */

  state = GETTING_DEV_TYPE;

  while ( state != DONE ) {

    /* printf( "state = %-d\n", state ); */

    stat = nextToken( inFile );
    if ( stat == ERR_EOF ) {
      if ( state != GETTING_DEV_TYPE ) {
        printf( "Unexpected eof at line %-d\n", g_line );
        return stat;
      }
      else {
        return ERR_OK;
      }
    }
    else if ( !( stat & 1 ) ) {
      return stat;
    }

    /* printf( "token = [%s]\n", g_tk ); */

    switch ( state ) {

    case GETTING_DEV_TYPE:

      if ( ( g_tk[0] == '{' ) || ( g_tk[0] == '}' ) ) {
        printf( "Syntax error at line %-d\n", g_line );
        return ERR_SYNTAX;
      }

      strncpy( devType, g_tk, 31 );
      devType[31] = 0;
      /* printf( "Dev type = [%s]\n", devType ); */

      if ( ( strcmp( devType, "generic" ) != 0 ) &&
           ( strcmp( devType, "serial" ) != 0 ) &&
           ( strcmp( devType, "plc5Enet" ) != 0 ) &&
           ( strcmp( devType, "df1" ) != 0 )    ) {
        printf( "Unknown device type at line %-d\n", g_line );
        return ERR_UNKNOWN_DEV_TYPE;
      }

      state = GETTING_DEV_NAME;

      break;

    case GETTING_DEV_NAME:

      if ( ( g_tk[0] == '{' ) || ( g_tk[0] == '}' ) ) {
        printf( "Syntax error at line %-d\n", g_line );
        return ERR_SYNTAX;
      }

      strncpy( devName, g_tk, 31 );
      devName[31] = 0;
      /* printf( "Dev name = [%s]\n", devName ); */

      /* allocate new device node */
      curDev = (devListPtr) calloc( 1, sizeof(devListType) );
      if ( !curDev ) return ERR_NOMEM;

      curDev->name = (char *) calloc( 1, strlen(devName)+1 );
      if ( !curDev->name ) return ERR_NOMEM;
      strcpy( curDev->name, devName );

      curDev->className = (char *) calloc( 1, strlen(devType)+1 );
      if ( !curDev->className ) return ERR_NOMEM;
      strcpy( curDev->className, devType );

      stat = avl_init_tree( compare_prop_nodes,
       compare_prop_key, copy_prop_nodes, &curDev->properties );
      if ( !( stat & 1 ) ) {
        printf( "Error [%-d] from avl_init_tree\n", stat );
        return ERR_FAIL;
      }

      stat = avl_insert_node( g_devices, (void *) curDev, &dup );
      if ( !( stat & 1 ) ) {
        printf( "Error [%-d] from avl_insert_node\n", stat );
        return ERR_FAIL;
      }
      if ( dup ) {
        printf( "Duplicate device: %-s at line %-d\n", curDev->name, g_line );
        return ERR_FAIL;
      }

      state = GETTING_DEV_OPEN_BRACE;

      break;

    case GETTING_DEV_OPEN_BRACE:

      if ( g_tk[0] == '}' ) {
        printf( "Syntax error at line %-d\n", g_line );
        return ERR_SYNTAX;
      }

      if ( g_tk[0] != '{' ) {
        printf( "Syntax error at line %-d\n", g_line );
        return ERR_SYNTAX;
      }

      state = GETTING_DEV_UNIT_OR_CLOSE_BRACE;

      break;

    case GETTING_DEV_UNIT_OR_CLOSE_BRACE:

      if ( g_tk[0] == '{' ) {
        printf( "Syntax error at line %-d\n", g_line );
        return ERR_SYNTAX;
      }

      if ( g_tk[0] == '}' ) {
        state = GETTING_DEV_TYPE;
        break;
      }

      curProp = (propListPtr) calloc( 1, sizeof(propListType) );
      if ( !curProp ) return ERR_NOMEM;

      strncpy( devUnit, g_tk, 7 );
      devUnit[7] = 0;
      /* printf( "unit = %s\n", devUnit ); */

      curProp->unit = calloc( 1, strlen(devUnit)+1 );
      if ( !curProp->unit ) return ERR_NOMEM;
      strcpy( curProp->unit, devUnit );

      state = GETTING_UNIT_OPEN_BRACE;
      break;

    case GETTING_UNIT_OPEN_BRACE:

      if ( g_tk[0] == '}' ) {
        printf( "Syntax error at line %-d\n", g_line );
        return ERR_SYNTAX;
      }

      if ( g_tk[0] != '{' ) {
        printf( "Syntax error at line %-d\n", g_line );
        return ERR_SYNTAX;
      }
      state = GETTING_UNIT_KEYWORDS_OR_CLOSE_BRACE;
      break;

    case GETTING_UNIT_KEYWORDS_OR_CLOSE_BRACE:

      if ( g_tk[0] == '{' ) {
        printf( "Syntax error at line %-d\n", g_line );
        return ERR_SYNTAX;
      }

      if ( g_tk[0] == '}' ) {

	/* printf( "insert prop, unit = [%s]\n", curProp->unit ); */

        stat = avl_insert_node( curDev->properties, (void *) curProp, &dup );
        if ( !( stat & 1 ) ) {
          printf( "Error [%-d] from avl_insert_node\n", stat );
          return ERR_FAIL;
        }
        if ( dup ) {
          printf( "Duplicate unit: %s[%s] at line %-d\n",
          curDev->name, curProp->unit, g_line );
          return ERR_FAIL;
        }

        state = GETTING_DEV_UNIT_OR_CLOSE_BRACE;

        break;

      }

      strncpy( keyword, g_tk, 63 );
      keyword[63] = 0;

      /* printf( "keyword = [%s]\n", keyword ); */

      state = GETTING_UNIT_KEYWORD_VALUE;

      break;

    case GETTING_UNIT_KEYWORD_VALUE:

      if ( ( g_tk[0] == '{' ) || ( g_tk[0] == '}' ) ) {
        printf( "Syntax error at line %-d\n", g_line );
        return ERR_SYNTAX;
      }

      strncpy( value, g_tk, 63 );
      value[63] = 0;
      /* printf( "1st keyword = [%s]\n", keyword ); */
      /* printf( "1st value = [%s]\n", value ); */

      stat = setKeyword( curDev->className, curProp, keyword, value );
      if ( !( stat & 1 ) ) {
        if ( stat == ERR_SYNTAX )
          printf( "Syntax error at line %-d\n", g_line );
        return stat;
      }

      state = GETTING_UNIT_KEYWORDS_OR_COLON_OR_CLOSE_BRACE;

      break;

    case GETTING_UNIT_KEYWORDS_OR_COLON_OR_CLOSE_BRACE:

      if ( g_tk[0] == '{' ) {
        printf( "Syntax error at line %-d\n", g_line );
        return ERR_SYNTAX;
      }
      else if ( g_tk[0] == '}' ) {

	/* printf( "insert prop, unit = [%s]\n", curProp->unit ); */

        stat = avl_insert_node( curDev->properties, (void *) curProp, &dup );
        if ( !( stat & 1 ) ) {
          printf( "Error [%-d] from avl_insert_node\n", stat );
          return ERR_FAIL;
        }
        if ( dup ) {
          printf( "Duplicate unit: %s[%s] at line %-d\n",
          curDev->name, curProp->unit, g_line );
          return ERR_FAIL;
        }

        state = GETTING_DEV_UNIT_OR_CLOSE_BRACE;

        break;

      }
      else if ( g_tk[0] == ':' ) {

        state = GETTING_UNIT_KEYWORD_2ND_VALUE;

        break;

      }

      strncpy( keyword, g_tk, 63 );
      keyword[63] = 0;
      /* printf( "keyword = [%s]\n", keyword ); */

      state = GETTING_UNIT_KEYWORD_VALUE;

      break;

    case GETTING_UNIT_KEYWORD_2ND_VALUE:

      if ( ( g_tk[0] == '{' ) || ( g_tk[0] == '}' ) ) {
        printf( "Syntax error at line %-d\n", g_line );
        return ERR_SYNTAX;
      }

      strncpy( value, g_tk, 63 );
      value[63] = 0;
      /* printf( "2nd keyword = [%s]\n", keyword ); */
      /* printf( "2nd value = [%s]\n", value ); */

      stat = setKeyword2( curDev->className, curProp, keyword, value );
      if ( !( stat & 1 ) ) {
        if ( stat == ERR_SYNTAX )
          printf( "Syntax error at line %-d\n", g_line );
        return stat;
      }

      state = GETTING_UNIT_KEYWORDS_OR_CLOSE_BRACE;

      break;

    }

  }

  return 1;

}

static void encodeKeyword (
  char *devType,
  char *genericKeyword,
  char *deviceKeyword,
  int max
) {

  strncpy( deviceKeyword, genericKeyword, max );
  deviceKeyword[max] = 0;

  if ( strcmp( devType, "serial" ) == 0 ) {

    if ( strcmp( genericKeyword, "prop0" ) == 0 ) {
      strncpy( deviceKeyword, "baud", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop1" ) == 0 ) {
      strncpy( deviceKeyword, "data bits", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop2" ) == 0 ) {
      strncpy( deviceKeyword, "stop bits", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop3" ) == 0 ) {
      strncpy( deviceKeyword, "parity", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop6" ) == 0 ) {
      strncpy( deviceKeyword, "ip_addr", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop7" ) == 0 ) {
      strncpy( deviceKeyword, "ip_port", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop8" ) == 0 ) {
      strncpy( deviceKeyword, "ip_proto", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop9" ) == 0 ) {
      strncpy( deviceKeyword, "dev_addr", max );
      deviceKeyword[max] = 0;
    }

  }
  else if ( strcmp( devType, "df1" ) == 0 ) {

    if ( strcmp( genericKeyword, "prop0" ) == 0 ) {
      strncpy( deviceKeyword, "baud", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop1" ) == 0 ) {
      strncpy( deviceKeyword, "data bits", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop2" ) == 0 ) {
      strncpy( deviceKeyword, "stop bits", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop3" ) == 0 ) {
      strncpy( deviceKeyword, "parity", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop4" ) == 0 ) {
      strncpy( deviceKeyword, "df1 addr", max );
      deviceKeyword[max] = 0;
    }

  }
  else if ( strcmp( devType, "plc5Enet" ) == 0 ) {

    if ( strcmp( genericKeyword, "prop0" ) == 0 ) {
      strncpy( deviceKeyword, "ip_addr", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop1" ) == 0 ) {
      strncpy( deviceKeyword, "port", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop2" ) == 0 ) {
      strncpy( deviceKeyword, "plc5_tbl_addr", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop3" ) == 0 ) {
      strncpy( deviceKeyword, "plc5_tbl_len", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop4" ) == 0 ) {
      strncpy( deviceKeyword, "scan_rate", max );
      deviceKeyword[max] = 0;
    }
    if ( strcmp( genericKeyword, "prop5" ) == 0 ) {
      strncpy( deviceKeyword, "direction", max );
      deviceKeyword[max] = 0;
    }

  }

}

static void showProperties (
  char *devType,
  propListPtr curProp
) {

char propName[63+1];

  if ( curProp->deviceName ) {
    printf( "    Device name = %s\n", curProp->deviceName );
  }

  if ( curProp->prop0.name ) {
    encodeKeyword( devType, curProp->prop0.name, propName, 63 );
    printf( "    %s = ", propName );
    if ( curProp->prop0.value ) {
      printf( "%s\n", curProp->prop0.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->prop1.name ) {
    encodeKeyword( devType, curProp->prop1.name, propName, 63 );
    printf( "    %s = ", propName );
    if ( curProp->prop1.value ) {
      printf( "%s\n", curProp->prop1.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->prop2.name ) {
    encodeKeyword( devType, curProp->prop2.name, propName, 63 );
    printf( "    %s = ", propName );
    if ( curProp->prop2.value ) {
      printf( "%s\n", curProp->prop2.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->prop3.name ) {
    encodeKeyword( devType, curProp->prop3.name, propName, 63 );
    printf( "    %s = ", propName );
    if ( curProp->prop3.value ) {
      printf( "%s\n", curProp->prop3.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->prop4.name ) {
    encodeKeyword( devType, curProp->prop4.name, propName, 63 );
    printf( "    %s = ", propName );
    if ( curProp->prop4.value ) {
      printf( "%s\n", curProp->prop4.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->prop5.name ) {
    encodeKeyword( devType, curProp->prop5.name, propName, 63 );
    printf( "    %s = ", propName );
    if ( curProp->prop5.value ) {
      printf( "%s\n", curProp->prop5.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->prop6.name ) {
    encodeKeyword( devType, curProp->prop6.name, propName, 63 );
    printf( "    %s = ", propName );
    if ( curProp->prop6.value ) {
      printf( "%s\n", curProp->prop6.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->prop7.name ) {
    encodeKeyword( devType, curProp->prop7.name, propName, 63 );
    printf( "    %s = ", propName );
    if ( curProp->prop7.value ) {
      printf( "%s\n", curProp->prop7.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->prop8.name ) {
    encodeKeyword( devType, curProp->prop8.name, propName, 63 );
    printf( "    %s = ", propName );
    if ( curProp->prop8.value ) {
      printf( "%s\n", curProp->prop8.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->prop9.name ) {
    encodeKeyword( devType, curProp->prop9.name, propName, 63 );
    printf( "    %s = ", propName );
    if ( curProp->prop9.value ) {
      printf( "%s\n", curProp->prop9.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->mem0.name ) {
    printf( "    %s = ", curProp->mem0.name );
    if ( curProp->mem0.value ) {
      printf( "%s", curProp->mem0.value );
    }
    else {
      printf( "[NULL]" );
    }
    if ( curProp->mem0.value2 ) {
      printf( " : %s\n", curProp->mem0.value2 );
    }
    else {
      printf( "\n" );
    }
  }
  if ( curProp->mem1.name ) {
    printf( "    %s = ", curProp->mem1.name );
    if ( curProp->mem1.value ) {
      printf( "%s", curProp->mem1.value );
    }
    else {
      printf( "[NULL]" );
    }
    if ( curProp->mem1.value2 ) {
      printf( " : %s\n", curProp->mem1.value2 );
    }
    else {
      printf( "\n" );
    }
  }
  if ( curProp->mem2.name ) {
    printf( "    %s = ", curProp->mem2.name );
    if ( curProp->mem2.value ) {
      printf( "%s", curProp->mem2.value );
    }
    else {
      printf( "[NULL]" );
    }
    if ( curProp->mem2.value2 ) {
      printf( " : %s\n", curProp->mem2.value2 );
    }
    else {
      printf( "\n" );
    }
  }

  if ( curProp->io0.name ) {
    printf( "    %s = ", curProp->io0.name );
    if ( curProp->io0.value ) {
      printf( "%s", curProp->io0.value );
    }
    else {
      printf( "[NULL]" );
    }
    if ( curProp->io0.value2 ) {
      printf( " : %s\n", curProp->io0.value2 );
    }
    else {
      printf( "\n" );
    }
  }
  if ( curProp->io1.name ) {
    printf( "    %s = ", curProp->io1.name );
    if ( curProp->io1.value ) {
      printf( "%s", curProp->io1.value );
    }
    else {
      printf( "[NULL]" );
    }
    if ( curProp->io1.value2 ) {
      printf( " : %s\n", curProp->io1.value2 );
    }
    else {
      printf( "\n" );
    }
  }
  if ( curProp->io2.name ) {
    printf( "    %s = ", curProp->io2.name );
    if ( curProp->io2.value ) {
      printf( "%s", curProp->io2.value );
    }
    else {
      printf( "[NULL]" );
    }
    if ( curProp->io2.value2 ) {
      printf( " : %s\n", curProp->io2.value2 );
    }
    else {
      printf( "\n" );
    }
  }

  if ( curProp->int0.name ) {
    printf( "    %s = ", curProp->int0.name );
    if ( curProp->int0.value ) {
      printf( "%s\n", curProp->int0.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }
  if ( curProp->int1.name ) {
    printf( "    %s = ", curProp->int1.name );
    if ( curProp->int1.value ) {
      printf( "%s\n", curProp->int1.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }
  if ( curProp->int2.name ) {
    printf( "    %s = ", curProp->int2.name );
    if ( curProp->int2.value ) {
      printf( "%s\n", curProp->int2.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->dma0.name ) {
    printf( "    %s = ", curProp->dma0.name );
    if ( curProp->dma0.value ) {
      printf( "%s\n", curProp->dma0.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }
  if ( curProp->dma1.name ) {
    printf( "    %s = ", curProp->dma1.name );
    if ( curProp->dma1.value ) {
      printf( "%s\n", curProp->dma1.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }
  if ( curProp->dma2.name ) {
    printf( "    %s = ", curProp->dma2.name );
    if ( curProp->dma2.value ) {
      printf( "%s\n", curProp->dma2.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->bus.name ) {
    printf( "    %s = ", curProp->bus.name );
    if ( curProp->bus.value ) {
      printf( "%s\n", curProp->bus.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

  if ( curProp->node.name ) {
    printf( "    %s = ", curProp->node.name );
    if ( curProp->node.value ) {
      printf( "%s\n", curProp->node.value );
    }
    else {
      printf( "[NULL]\n" );
    }
  }

}

void devMgrGenReport ( void ) {

int stat;
devListPtr curDev;
propListPtr curProp;

/* printf( "genReport\n" ); */

  stat = avl_get_first( g_devices, (void **) &curDev );

  while ( curDev ) {

    printf( "\n%s device: %s\n", curDev->className, curDev->name );

    stat = avl_get_first( curDev->properties, (void **) &curProp );

    while ( curProp ) {

      printf( "  Unit: %s\n", curProp->unit );
      showProperties( curDev->className, curProp );

      stat = avl_get_next( curDev->properties, (void **) &curProp );

    }

    stat = avl_get_next( g_devices, (void **) &curDev );

  }

}

void devMgrGenReport2 ( void ) {

static char *blank = "";

int stat;
propListPtr curProp;
char *className = blank;
char *name = blank;

printf( "genReport2\n" );

  stat = devMgrGetFirstFromAll( &className, &name, &curProp );
  while ( curProp ) {

    printf( "\n%s device: %s\n", className, name );

    printf( "  Unit: %s\n", curProp->unit );
    showProperties( className, curProp );

    stat = devMgrGetNextFromAll( &className, &name, &curProp );

  }

}

static int checkMem (
  propListPtr curProp
) {

numListPtr cur;
int n0, n1;

  if ( curProp->mem0.value ) {

    n0 = strtol( curProp->mem0.value, NULL, 0 );
    if ( curProp->mem0.value2 )
      n1 = strtol( curProp->mem0.value2, NULL, 0 );
    else
      n1 = n0;

    /* check against current list */

    cur = g_memHead->flink;
    while ( cur ) {
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "Memory address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_memTail->flink = cur;
    g_memTail = cur;
    g_memTail->flink = NULL;

  }

  if ( curProp->mem1.value ) {

    n0 = strtol( curProp->mem1.value, NULL, 0 );
    if ( curProp->mem1.value2 )
      n1 = strtol( curProp->mem1.value2, NULL, 0 );
    else
      n1 = n0;

    /* check against current list */

    cur = g_memHead->flink;
    while ( cur ) {
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "Memory address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_memTail->flink = cur;
    g_memTail = cur;
    g_memTail->flink = NULL;

  }

  if ( curProp->mem2.value ) {

    n0 = strtol( curProp->mem2.value, NULL, 0 );
    if ( curProp->mem2.value2 )
      n1 = strtol( curProp->mem2.value2, NULL, 0 );
    else
      n1 = n0;

    /* check against current list */

    cur = g_memHead->flink;
    while ( cur ) {
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "Memory address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_memTail->flink = cur;
    g_memTail = cur;
    g_memTail->flink = NULL;

  }

  return ERR_OK;

}

static int checkIo (
  propListPtr curProp
) {

numListPtr cur;
int n0, n1;

  if ( curProp->io0.value ) {

    n0 = strtol( curProp->io0.value, NULL, 0 );
    if ( curProp->io0.value2 )
      n1 = strtol( curProp->io0.value2, NULL, 0 );
    else
      n1 = n0;

    /* printf( "check %-d %-d\n", n0, n1 ); */
    /* check against current list */

    cur = g_ioHead->flink;
    while ( cur ) {
      /* printf( "   against %-d %-d\n", cur->i0, cur->i1 ); */
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "I/O address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_ioTail->flink = cur;
    g_ioTail = cur;
    g_ioTail->flink = NULL;

  }

  if ( curProp->io1.value ) {

    n0 = strtol( curProp->io1.value, NULL, 0 );
    if ( curProp->io1.value2 )
      n1 = strtol( curProp->io1.value2, NULL, 0 );
    else
      n1 = n0;

    /* printf( "check %-d %-d\n", n0, n1 ); */
    /* check against current list */

    cur = g_ioHead->flink;
    while ( cur ) {
      /* printf( "   against %-d %-d\n", cur->i0, cur->i1 ); */
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "I/O address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_ioTail->flink = cur;
    g_ioTail = cur;
    g_ioTail->flink = NULL;

  }

  if ( curProp->io2.value ) {

    n0 = strtol( curProp->io2.value, NULL, 0 );
    if ( curProp->io2.value2 )
      n1 = strtol( curProp->io2.value2, NULL, 0 );
    else
      n1 = n0;

    /* printf( "check %-d %-d\n", n0, n1 ); */
    /* check against current list */

    cur = g_ioHead->flink;
    while ( cur ) {
      /* printf( "   against %-d %-d\n", cur->i0, cur->i1 ); */
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "I/O address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_ioTail->flink = cur;
    g_ioTail = cur;
    g_ioTail->flink = NULL;

  }

  return ERR_OK;

}

static int checkInt (
  propListPtr curProp
) {

numListPtr cur;
int n0, n1;

  if ( curProp->int0.value ) {

    n0 = strtol( curProp->int0.value, NULL, 0 );
    if ( curProp->int0.value2 )
      n1 = strtol( curProp->int0.value2, NULL, 0 );
    else
      n1 = n0;

    /* printf( "check %-d %-d\n", n0, n1 ); */
    /* check against current list */

    cur = g_intHead->flink;
    while ( cur ) {
      /* printf( "   against %-d %-d\n", cur->i0, cur->i1 ); */
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "I/O address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_intTail->flink = cur;
    g_intTail = cur;
    g_intTail->flink = NULL;

  }

  if ( curProp->int1.value ) {

    n0 = strtol( curProp->int1.value, NULL, 0 );
    if ( curProp->int1.value2 )
      n1 = strtol( curProp->int1.value2, NULL, 0 );
    else
      n1 = n0;

    /* printf( "check %-d %-d\n", n0, n1 ); */
    /* check against current list */

    cur = g_intHead->flink;
    while ( cur ) {
      /* printf( "   against %-d %-d\n", cur->i0, cur->i1 ); */
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "I/O address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_intTail->flink = cur;
    g_intTail = cur;
    g_intTail->flink = NULL;

  }

  if ( curProp->int2.value ) {

    n0 = strtol( curProp->int2.value, NULL, 0 );
    if ( curProp->int2.value2 )
      n1 = strtol( curProp->int2.value2, NULL, 0 );
    else
      n1 = n0;

    /* printf( "check %-d %-d\n", n0, n1 ); */
    /* check against current list */

    cur = g_intHead->flink;
    while ( cur ) {
      /* printf( "   against %-d %-d\n", cur->i0, cur->i1 ); */
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "I/O address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_intTail->flink = cur;
    g_intTail = cur;
    g_intTail->flink = NULL;

  }

  return ERR_OK;

}

static int checkDma (
  propListPtr curProp
) {

numListPtr cur;
int n0, n1;

  if ( curProp->dma0.value ) {

    n0 = strtol( curProp->dma0.value, NULL, 0 );
    if ( curProp->dma0.value2 )
      n1 = strtol( curProp->dma0.value2, NULL, 0 );
    else
      n1 = n0;

    /* printf( "check %-d %-d\n", n0, n1 ); */
    /* check against current list */

    cur = g_dmaHead->flink;
    while ( cur ) {
      /* printf( "   against %-d %-d\n", cur->i0, cur->i1 ); */
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "I/O address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_dmaTail->flink = cur;
    g_dmaTail = cur;
    g_dmaTail->flink = NULL;

  }

  if ( curProp->dma1.value ) {

    n0 = strtol( curProp->dma1.value, NULL, 0 );
    if ( curProp->dma1.value2 )
      n1 = strtol( curProp->dma1.value2, NULL, 0 );
    else
      n1 = n0;

    /* printf( "check %-d %-d\n", n0, n1 ); */
    /* check against current list */

    cur = g_dmaHead->flink;
    while ( cur ) {
      /* printf( "   against %-d %-d\n", cur->i0, cur->i1 ); */
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "I/O address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_dmaTail->flink = cur;
    g_dmaTail = cur;
    g_dmaTail->flink = NULL;

  }

  if ( curProp->dma2.value ) {

    n0 = strtol( curProp->dma2.value, NULL, 0 );
    if ( curProp->dma2.value2 )
      n1 = strtol( curProp->dma2.value2, NULL, 0 );
    else
      n1 = n0;

    /* printf( "check %-d %-d\n", n0, n1 ); */
    /* check against current list */

    cur = g_dmaHead->flink;
    while ( cur ) {
      /* printf( "   against %-d %-d\n", cur->i0, cur->i1 ); */
      if ( ( ( cur->i0 <= n0 ) && ( cur->i1 >= n0 ) ) ||
           ( ( cur->i0 <= n1 ) && ( cur->i1 >= n1 ) ) ||
           ( ( n0 <= cur->i0 ) && ( n1 >= cur->i0 ) ) ||
           ( ( n0 <= cur->i1 ) && ( n1 >= cur->i1 ) )   ) {
        printf( "I/O address conflict for unit %s", curProp->unit );
        return ERR_FAIL;
      }
      cur = cur->flink;
    }

    /* new node */

    cur = (numListPtr) calloc( 1, sizeof(numListType) );
    if ( !cur ) {
      printf( "checkMem, Insufficient virtual memory\n" );
      return ERR_NOMEM;
    }

    cur->i0 = n0;

    cur->i1 = n1;

    /* add to list */
    g_dmaTail->flink = cur;
    g_dmaTail = cur;
    g_dmaTail->flink = NULL;

  }

  return ERR_OK;

}

#if 0
static int checkBus (
  propListPtr curProp
) {

numListPtr cur;
int n0;

  return ERR_OK;

  n0 = strtol( curProp->bus.value, NULL, 0 );

  /* printf( "check %-d %-d\n", n0 ); */
  /* check against current list */

  cur = g_busHead->flink;
  while ( cur ) {
    /* printf( "   against %-d\n", cur->i0 ); */
    if ( cur->i0 == n0 ) {
      printf( "Bus address conflict for unit %s", curProp->unit );
      return ERR_FAIL;
    }
    cur = cur->flink;
  }

  /* new node */

  cur = (numListPtr) calloc( 1, sizeof(numListType) );
  if ( !cur ) {
    printf( "checkMem, Insufficient virtual memory\n" );
    return ERR_NOMEM;
  }

  cur->i0 = n0;

  /* add to list */
  g_busTail->flink = cur;
  g_busTail = cur;
  g_busTail->flink = NULL;

  return ERR_OK;

}
#endif


static int verifyList ( void ) {

int stat, retStat = ERR_OK;
devListPtr curDev;
propListPtr curProp;

/* printf( "verifyList\n" ); */

/* create sentinel nodes */

  g_memHead = (numListPtr) calloc( 1, sizeof(numListType) );
  if ( !g_memHead ) {
    printf( "Insufficient virtual memory\n" );
    exit(0);
  }
  g_memTail = g_memHead;
  g_memTail->flink = NULL;

  g_ioHead = (numListPtr) calloc( 1, sizeof(numListType) );
  if ( !g_ioHead ) {
    printf( "Insufficient virtual memory\n" );
    exit(0);
  }
  g_ioTail = g_ioHead;
  g_ioTail->flink = NULL;

  g_intHead = (numListPtr) calloc( 1, sizeof(numListType) );
  if ( !g_intHead ) {
    printf( "Insufficient virtual memory\n" );
    exit(0);
  }
  g_intTail = g_intHead;
  g_intTail->flink = NULL;

  g_dmaHead = (numListPtr) calloc( 1, sizeof(numListType) );
  if ( !g_dmaHead ) {
    printf( "Insufficient virtual memory\n" );
    exit(0);
  }
  g_dmaTail = g_dmaHead;
  g_dmaTail->flink = NULL;

  g_busHead = (numListPtr) calloc( 1, sizeof(numListType) );
  if ( !g_busHead ) {
    printf( "Insufficient virtual memory\n" );
    exit(0);
  }
  g_busTail = g_busHead;
  g_busTail->flink = NULL;

  /* build lists and check for overlaps */

  stat = avl_get_first( g_devices, (void **) &curDev );

  while ( curDev ) {

    stat = avl_get_first( curDev->properties, (void **) &curProp );

    while ( curProp ) {

      stat = checkMem( curProp );
      if ( !( stat & 1 ) ) {
        printf( " of %s\n", curDev->name );
        retStat = stat;
      }

      stat = checkIo( curProp );
      if ( !( stat & 1 ) ) {
        printf( " of %s\n", curDev->name );
        retStat = stat;
      }

      stat = checkInt( curProp );
      if ( !( stat & 1 ) ) {
        printf( " of %s\n", curDev->name );
        retStat = stat;
      }

      stat = checkDma( curProp );
      if ( !( stat & 1 ) ) {
        printf( " of %s\n", curDev->name );
        retStat = stat;
      }

      stat = avl_get_next( curDev->properties, (void **) &curProp );

    }

    stat = avl_get_next( g_devices, (void **) &curDev );

  }

  return retStat;

}

void devMgrVersion (
  int *major,
  int *minor,
  int *release
) {

  *major = g_major;
  *minor = g_minor;
  *release = g_release;

}

long epicsShareAPI devMgrInit (
  char *fileName
) {

FILE *inFile;
int stat;

#if 0
  printf( "devMgrInit, fileName = [%s]\n", fileName );
#endif

  if ( g_init_done ) return 0;

  inFile = fopen( fileName, "r" );
  if ( !inFile ) {
    perror( "File open failure" );
    return ERR_FAIL;
  }

  /* create device list */

  stat = avl_init_tree( compare_dev_nodes,
   compare_dev_key, copy_dev_nodes, &g_devices );
  if ( !( stat & 1 ) ) {
    printf( "avl_init_tree failed\n" );
    goto errExit;
  }

  stat = buildListFromFile( inFile );
  if ( !( stat & 1 ) ) goto errExit;

  stat = verifyList();
  if ( !( stat & 1 ) ) goto errExit;


/* DBM */  
#if 0
  devMgrGenReport();  
#endif

  fclose( inFile );

  g_init_done = 1;

  return 0;

errExit:

/* DBM */  
/*  devMgrGenReport(); */
  
  fclose( inFile );
  printf( "Execution aborted\n" );
  return ERR_FAIL;

}

int devMgrGetFirstFromAll (
  char **className,
  char **name,
  propListPtr *ptr
) {

int stat;

  g_getNextFromAll = 0;

  *ptr = (propListPtr) NULL;

  if ( !g_init_done ) return ERR_NOINIT;

  stat = avl_get_first( g_devices, (void **) &g_curDevFromAll );
  if ( !( stat & 1 ) ) return stat;

  if ( !g_curDevFromAll ) {
    return ERR_FAIL;
  }

  *className = g_curDevFromAll->className;
  *name = g_curDevFromAll->name;

  stat = avl_get_first( g_curDevFromAll->properties, (void **) ptr );

  if ( *ptr ) g_getNextFromAll = 1;

  return stat;

}

int devMgrGetNextFromAll (
  char **className,
  char **name,
  propListPtr *ptr
) {

int stat;

  *ptr = (propListPtr) NULL;

  if ( !g_init_done ) return ERR_NOINIT;

  if ( g_getNextFromAll ) {

    stat = avl_get_next( g_curDevFromAll->properties, (void **) ptr );

    if ( !( stat & 1 ) || !(*ptr) ) {
      g_getNextFromAll = 0;
    }

  }

  if ( !g_getNextFromAll ) {

    stat = avl_get_next( g_devices, (void **) &g_curDevFromAll );
    if ( !( stat & 1 ) ) return stat;

    if ( !g_curDevFromAll ) {
      return ERR_FAIL;
    }

    *className = g_curDevFromAll->className;
    *name = g_curDevFromAll->name;

    stat = avl_get_first( g_curDevFromAll->properties, (void **) ptr );

    if ( ( stat & 1 ) && *ptr ) g_getNextFromAll = 1;

  }

  return stat;

}

int devMgrGetFirst (
  char *name,
  propListPtr *ptr
) {

int stat;

  *ptr = (propListPtr) NULL;

  if ( !g_init_done ) return ERR_NOINIT;

  stat = avl_get_match( g_devices, (void *) name, (void **) &g_curDev );
  if ( !( stat & 1 ) ) return stat;

  if ( !g_curDev ) {
    ptr = NULL;
    return ERR_FAIL;
  }

  stat = avl_get_first( g_curDev->properties, (void **) ptr );
  return stat;

}

int devMgrGetNext (
  char *name,
  propListPtr *ptr
) {

int stat;

  *ptr = (propListPtr) NULL;

  if ( !g_init_done ) return ERR_NOINIT;

  stat = avl_get_next( g_curDev->properties, (void **) ptr );
  return stat;

}

#ifdef __INCLUDE_MAIN__

#ifdef __vxworks__

int dm_main (
  char *f,
  char *d
) {

#else

int main (
  int argc,
  char **argv
) {

#endif

propListPtr prop;
int stat;

#ifdef __vxworks__

int argc = 3;
static char *argv[3] = { "dm_main", 0, 0 };

  argv[1] = f;
  argv[2] = d;

  if ( d == NULL ) argc = 2;

#endif

  if ( argc < 2 ) {
    printf( "[Version %-d.%-d.%-d] Usage: devManager <conf file> [<device name>]\n",
     g_major, g_minor, g_release );
    exit(0);
  }

  stat = devMgrInit( argv[1] );
  if ( stat ) return 0;

  if ( argc == 2 ) {
    genReport2();
  }
  else {

    printf( "\nDevice: %s\n\n", argv[2] );

    stat = devMgrGetFirst( argv[2], &prop );

    if ( !prop ) {
      printf( "  [No such device found]\n" );
    }

    while ( prop ) {

      printf( "  unit: %s\n", prop->unit );
      if ( prop->deviceName )
        printf( "    device name = %s\n", prop->deviceName );
      if ( prop->prop0.value )
        printf( "    %s = %s\n", prop->prop0.name, prop->prop0.value );
      if ( prop->prop1.value )
        printf( "    %s = %s\n", prop->prop1.name, prop->prop1.value );
      if ( prop->prop2.value )
        printf( "    %s = %s\n", prop->prop2.name, prop->prop2.value );
      if ( prop->prop3.value )
        printf( "    %s = %s\n", prop->prop3.name, prop->prop3.value );
      if ( prop->prop4.value )
        printf( "    %s = %s\n", prop->prop4.name, prop->prop4.value );
      if ( prop->prop5.value )
        printf( "    %s = %s\n", prop->prop5.name, prop->prop5.value );
      if ( prop->prop6.value )
        printf( "    %s = %s\n", prop->prop6.name, prop->prop6.value );
      if ( prop->prop7.value )
        printf( "    %s = %s\n", prop->prop7.name, prop->prop7.value );
      if ( prop->prop8.value )
        printf( "    %s = %s\n", prop->prop8.name, prop->prop8.value );
      if ( prop->prop9.value )
        printf( "    %s = %s\n", prop->prop9.name, prop->prop9.value );

      if ( prop->io0.value ) {
        printf( "    %s = %s", prop->io0.name, prop->io0.value );
        if ( prop->io0.value2 )
          printf( " : %s\n", prop->io0.value2 );
        else
          printf( "\n" );
      }

      if ( prop->io1.value ) {
          printf( "    %s = %s", prop->io1.name, prop->io1.value );
        if ( prop->io1.value2 )
          printf( " : %s\n", prop->io1.value2 );
        else
          printf( "\n" );
      }

      if ( prop->io2.value ) {
          printf( "    %s = %s", prop->io2.name, prop->io2.value );
        if ( prop->io2.value2 )
          printf( " : %s\n", prop->io2.value2 );
        else
          printf( "\n" );
      }

      printf( "\n" );

      stat = devMgrGetNext( argv[2], &prop );

    }

  }

  printf( "\n" );

  return 0;

}

#endif
