#include <sys/time.h>
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include "errno.h"

#include "ornl_sys_types.h"

#define epicsExportSharedSymbols

#include "cadef.h"
#include "epicsThread.h"
#include "epicsMutex.h"

#define SNAP_SUCCESS	1
#define SNAP_ERROR	2
#define SNAP_WARNING	3
#define SNAP_REPL_ERROR	0x100

static int g_major = 2, g_minor = 1, g_release = 0;

typedef struct repl_tag {
  struct repl_tag *flink;
  char *old;
  char *new;
} REPL_LIST_TYPE, *REPL_LIST_PTR;

typedef struct force_tag {
  struct force_tag *flink;
  char *name;
  char *value;
} FORCE_LIST_TYPE, *FORCE_LIST_PTR;

typedef struct ca_list_tag {
  struct ca_list_tag *flink;
  chid id;
  char *name;
} CA_LIST_TYPE, *CA_LIST_PTR;

static REPL_LIST_PTR g_rep_head, g_rep_tail;
static FORCE_LIST_PTR g_force_head, g_force_tail;
static FILE *g_db_list_f, *g_out_f;
static int g_in = 0, g_out = 0;
static char g_msg[255+1];
static int g_snap_first_time = 1;
static CA_LIST_PTR g_ca_list_head, g_ca_list_tail;

static char g_arg1[255+1], g_arg2[255+1], g_arg3[255+1];

static void show_error (
  int error_code,
  char *msg
) {

  printf( "%s\n", msg );

}

static void show_forced (
  const char *name,
  char *value
) {

  /* printf( "%s forced to [%s]\n", name, value ); */

}

static int db_list_init( void ) {

int stat;
chid chix;
char db_list_file[255+1];

  strncpy( db_list_file, g_arg1, 255 );
  db_list_file[255] = 0;

  if ( db_list_file[0] == '@' ) {

    stat = ca_search( &db_list_file[1], &chix );
    if ( stat != ECA_NORMAL ) {
      printf( "ca_search failed for [%s]\n", &db_list_file[1] );
      return 0;
    }
    stat = ca_pend_io( 5.0 );
    ca_poll();
    if ( stat != ECA_NORMAL ) {
      printf( "ca_pend_io failed for [%s]\n", &db_list_file[1] );
      return 0;
    }

    stat = ca_get( DBR_STRING, chix, db_list_file );
    if ( stat != ECA_NORMAL ) {
      printf( "ca_get failed for [%s]\n", &db_list_file[1] );
      return 0;
    }
    stat = ca_pend_io( 5.0 );
    ca_poll();
    if ( stat != ECA_NORMAL ) {
      printf( "ca_pend_io failed for [%s]\n", &db_list_file[1] );
      return 0;
    }

    strncat( db_list_file, ".list", 255 );

  }

  if ( g_snap_first_time ) {
    printf( "Input list file is [%s]\n", db_list_file );
  }

  g_db_list_f = fopen( db_list_file, "r" );
  if ( !g_db_list_f ) return 0;

  return 1;

}

static int db_list_term( void ) {

int stat;

  stat = fclose( g_db_list_f );

  if ( stat )
    return 0;
  else
    return 1;

}

static int empty (
  char *string
) {

  return ( strspn( string, " \t\n" ) > 0 );

}

static int comment (
  char *string
) {

  return ( string[0] == '#' );

}

static int db_list_get_next_db (
  char *name,
  char *replacement_name,
  int max,
  char *forcedValue,
  int forcedValueMax,
  int *eof
) {

int i, skip;
char string[255+1];
char *result, *tk;

  do {

    result = fgets( string, max, g_db_list_f );

    if ( feof(g_db_list_f) ) {
      *eof = 1;
      return 0;
    }
    else
      *eof = 0;

    if ( empty( string ) )
      skip = TRUE;
    else if ( comment( string ) )
      skip = TRUE;
    else
      skip = FALSE;

  } while ( skip );

  tk = strtok( string, " \t\n" );
  if ( tk ) {
    for ( i=0; i<max && ( tk[i] != 0 ); i++ ) name[i] = tk[i];
    name[i] = 0;
  }
  else
    strncpy( name, "", max );

  tk = strtok( NULL, " \t\n" );
  if ( tk ) {
    for ( i=0; i<max && ( tk[i] != 0 ); i++ )
      replacement_name[i] = tk[i];
    replacement_name[i] = 0;
  }
  else
    strncpy( replacement_name, "", max );

  /* process forces */
  if ( strcmp( replacement_name, "-force" ) == 0 ) {

    tk = strtok( NULL, "\n" );
    if ( tk ) {
      for ( i=0; i<forcedValueMax && ( tk[i] != 0 ); i++ )
        forcedValue[i] = tk[i];
      forcedValue[i] = 0;
    }
    else
      strncpy( forcedValue, "", forcedValueMax );

  }
  else {

    strncpy( forcedValue, "", forcedValueMax );

  }

  return 1;

}

static int out_file_write_version ( void ) {

int stat;
char time_string[31+1];

  stat = fprintf( g_out_f, "%-d %-d %-d\n", g_major, g_minor, g_release );

  stat = sys_get_datetime_string( 31, time_string );
  time_string[31] = 0;
  stat = fprintf( g_out_f, "%s\n", time_string );

  return 1;

}

static int out_file_init(
  int postFix
) {

int stat;
char out_file[255+1], tmp[15+1];

  strncpy( out_file, g_arg2, 255 );
  out_file[255] = 0;

  strncat( out_file, ".", 255 );
  out_file[255] = 0;

  tmp[0] = (char) postFix + (char) 0x30;
  tmp[1] = 0;
  strncat( out_file, tmp, 255 );
  out_file[255] = 0;

  if ( g_snap_first_time ) {
    printf( "Output snap file is [%s]\n", out_file );
  }

  g_out_f = fopen( out_file, "w" );
  if ( !g_out_f ) return 0;

  stat = out_file_write_version();

  return 1;

}

static int out_file_term( void ) {

int stat;

  stat = fclose( g_out_f );

  if ( stat )
    return 0;
  else
    return 1;

}

static int out_file_write_line (
  const char *name,
  char *type,
  char *direction,
  char *value
) {

int stat;

  stat = fputs( type, g_out_f );
  stat = fputs( " ", g_out_f );
  stat = fputs( direction, g_out_f );
  stat = fputs( " VAL ", g_out_f );
  stat = fputs( name, g_out_f );
  stat = fputs( "\n", g_out_f );
  stat = fputs( value, g_out_f );
  stat = fputs( "\n", g_out_f );

  return 1;

}

static void format_special_characters(
  char *value
) {

char temp[255+1];
int i1, i2;
int need_to_copy = 0;

  /* replace ' with '' */

  for ( i1=0, i2=0; i1<=strlen(value); i1++ ) {

    temp[i2] = value[i1];
    if ( value[i1] == '\'' ) {
      if ( i2 < 255 ) i2++;
      temp[i2] = '\'';
      need_to_copy = 1;
    }
    if ( i2 < 255 ) i2++;

  }

  if ( need_to_copy ) strncpy( value, temp, 255 );

}

static void add_force (
  char *oneName,
  char *oneValue
) {

FORCE_LIST_PTR cur;

  cur = g_force_head->flink;
  while ( cur ) {
    if ( strcmp( cur->name, oneName ) == 0 ) {
      show_error( SNAP_WARNING, "duplicate force value - ignored" );
      return;
    }
    cur = cur->flink;
  }

  cur = (FORCE_LIST_PTR) calloc( 1, sizeof(FORCE_LIST_TYPE) );
  cur->name = (char *) malloc( strlen(oneName)+1 );
  strcpy( cur->name, oneName );
  cur->value = (char *) malloc( strlen(oneValue)+1 );
  strcpy( cur->value, oneValue );

  g_force_tail->flink = cur;
  g_force_tail = cur;
  g_force_tail->flink = NULL;

}

static int checkForce (
  const char *oneName,
  const char *nameWrittenToFile,
  char *curValue,
  int max
) {

FORCE_LIST_PTR cur;

  cur = g_force_head->flink;
  while ( cur ) {
    if ( strcmp( oneName, cur->name ) == 0 ) {
      strncpy( curValue, cur->value, max );
      curValue[max] = 0;
      show_forced( nameWrittenToFile, curValue );
      return 1;
    }
    cur = cur->flink;
  }

  return 0;

}

static int write_channel(
  chid chix,
  char *chanName
) {

int stat, ivalue;
char value[63+1], stype[31+1], svalue[63+1], direction[7+1];
int typ, ignore;
float fvalue;
double dvalue;
short shortValue;

  typ = ca_field_type( chix );

  ignore = 0;

  strcpy( direction, "ALL" );

  /* printf( "write_channel - [%s] - type = %-d\n", chanName, typ ); */

  if ( typ == DBR_LONG ) {

    strcpy( stype, "INTEGER" );

    stat = ca_get( DBR_LONG, chix, &ivalue );
    if ( stat != ECA_NORMAL ) {
      sprintf( g_msg, "Error reading value for %s", chanName );
      show_error( SNAP_ERROR, g_msg );
      return 0;
    }
    stat = ca_pend_io( 5.0 );
    ca_poll();
    if ( stat != ECA_NORMAL ) {
      sprintf( g_msg, "ca_pend_io failed for [%s]", chanName );
      show_error( SNAP_ERROR, g_msg );
      return 0;
    }

    sprintf( svalue, "%-d", ivalue );

  }
  else if ( typ == DBR_FLOAT ) {

    strcpy( stype, "REAL" );

    stat = ca_get( DBR_FLOAT, chix, &fvalue );
    if ( stat != ECA_NORMAL ) {
      sprintf( g_msg, "Error reading value for %s", chanName );
      show_error( SNAP_ERROR, g_msg );
      return 0;
    }
    stat = ca_pend_io( 5.0 );
    ca_poll();
    if ( stat != ECA_NORMAL ) {
      sprintf( g_msg, "ca_pend_io failed for [%s]", chanName );
      show_error( SNAP_ERROR, g_msg );
      return 0;
    }

    sprintf( svalue, "%-g", fvalue );

  }
  else if ( typ == DBR_DOUBLE ) {

    strcpy( stype, "DOUBLE" );

    stat = ca_get( DBR_DOUBLE, chix, &dvalue );
    if ( stat != ECA_NORMAL ) {
      sprintf( g_msg, "Error reading value for %s", chanName );
      show_error( SNAP_ERROR, g_msg );
      return 0;
    }
    stat = ca_pend_io( 5.0 );
    ca_poll();
    if ( stat != ECA_NORMAL ) {
      sprintf( g_msg, "ca_pend_io failed for [%s]", chanName );
      show_error( SNAP_ERROR, g_msg );
      return 0;
    }

    sprintf( svalue, "%-.9g", dvalue );

  }
  else if ( typ == DBR_ENUM ) {

    strcpy( stype, "BINARY" );

    stat = ca_get( DBR_ENUM, chix, &shortValue );
    if ( stat != ECA_NORMAL ) {
      sprintf( g_msg, "Error reading value for %s", chanName );
      show_error( SNAP_ERROR, g_msg );
      return 0;
    }
    stat = ca_pend_io( 5.0 );
    ca_poll();
    if ( stat != ECA_NORMAL ) {
      sprintf( g_msg, "ca_pend_io failed for [%s]", chanName );
      show_error( SNAP_ERROR, g_msg );
      return 0;
    }

    sprintf( svalue, "%-d", (int) shortValue );

  }
  else if ( typ == DBR_STRING ) {

    strcpy( stype, "STRING" );

    stat = ca_get( DBR_STRING, chix, value );
    if ( stat != ECA_NORMAL ) {
      sprintf( g_msg, "Error reading value for %s", chanName );
      show_error( SNAP_ERROR, g_msg );
      return 0;
    }
    stat = ca_pend_io( 5.0 );
    ca_poll();
    if ( stat != ECA_NORMAL ) {
      sprintf( g_msg, "ca_pend_io failed for [%s]", chanName );
      show_error( SNAP_ERROR, g_msg );
      return 0;
    }

    if ( strlen(value) == 0 ) {
      strcpy( svalue, "''" );
    }
    else {
      format_special_characters( value );
      strcpy( svalue, "\'" );
      strncat( svalue, value, 63 );
      strncat( svalue, "\'", 63 );
    }

  }
  else {

    ignore = 1;

  }

  if ( !ignore ) {

    checkForce( chanName, chanName, svalue, 63 );

    stat = out_file_write_line( chanName, stype, direction,
     svalue );
    if ( !stat ) {
      sprintf( g_msg, "Error from out_file_write_line for %s",
       chanName );
      show_error( SNAP_ERROR, g_msg );
      return 0;
    }

  }

  return 1;

}

static int waitUntilOutputsArmed ( void ) {

int stat;
chid pvId;
char name[255+1];
short outputsArmed;

  /* build full name of arm_outputs pv */
  strncpy( name, g_arg3, 255 );

  strncat( name, ":arm_outputs", 255 );
  name[255] = 0;

  /* resolve pv id */
  stat = ca_search( name, &pvId );
  if ( stat != ECA_NORMAL ) {
    printf( "ca_search failed for [%s]\n", name );
    return 2;
  }
  stat = ca_pend_io( 5.0 );
  ca_poll();
  if ( stat != ECA_NORMAL ) {
    printf( "ca_pend_io failed for [%s]\n", name );
    return 2;
  }

  /* wait until arm_outputs is non-zero */
  outputsArmed = 0;
  do {

    stat = ca_get( DBR_SHORT, pvId, &outputsArmed );
    if ( stat != ECA_NORMAL ) {
      printf( "Error reading value for %s", name );
      return 2;
    }
    stat = ca_pend_io( 5.0 );
    ca_poll();
    if ( stat != ECA_NORMAL ) {
      printf( "ca_pend_io failed for [%s]", name );
      return 2;
    }

    epicsThreadSleep( 1.0 );

  } while ( !outputsArmed );

  return 1;

}

static void snap_db_func (
  void *arg
) {

char chan_name[63+1], replacement_name[63+1], forcedValue[63+1];
int stat, eof, filePostfix;
CA_LIST_PTR cur;

enum ca_preemptive_callback_select {
  ca_disable_preemptive_callback,
  ca_enable_preemptive_callback
}; 

  stat = ca_context_create ( ca_enable_preemptive_callback );

  g_ca_list_head = (CA_LIST_PTR) calloc( 1, sizeof(CA_LIST_TYPE) );
  g_ca_list_tail = g_ca_list_head;
  g_ca_list_tail->flink = NULL;

  g_rep_head = (REPL_LIST_PTR) calloc( 1, sizeof(REPL_LIST_TYPE) );
  if ( !g_rep_head ) {
    show_error( SNAP_ERROR, "Insufficient virtual memory" );
    goto err_return;
  }
  g_rep_tail = g_rep_head;
  g_rep_tail->flink = NULL;

  g_force_head = (FORCE_LIST_PTR) calloc( 1, sizeof(FORCE_LIST_TYPE) );
  if ( !g_force_head ) {
    show_error( SNAP_ERROR, "Insufficient virtual memory" );
    goto err_return;
  }
  g_force_tail = g_force_head;
  g_force_tail->flink = NULL;

  stat = waitUntilOutputsArmed();
  if ( !( stat & 1 ) ) goto err_return;

  g_in = g_out = 1;

  /* build list of chids */
  stat = db_list_init();
  if ( !stat ) {
    show_error( SNAP_ERROR, "Error from db_list_init" );
    goto err_return;
  }

  stat = db_list_get_next_db( chan_name, replacement_name,
   63, forcedValue, 63, &eof );
  while ( !eof ) {

    /* printf( "chan_name = [%s]\n", chan_name ); */

    cur = (CA_LIST_PTR) calloc( 1, sizeof(CA_LIST_TYPE) );
    stat = ca_search( chan_name, &cur->id );
    if ( stat != ECA_NORMAL ) {
      printf( "ca_search failed for [%s]\n", chan_name );
      goto err_return;
    }
    stat = ca_pend_io( 5.0 );
    ca_poll();
    if ( stat != ECA_NORMAL ) {
      printf( "ca_pend_io failed for [%s]\n", chan_name );
      goto err_return;
    }

    cur->name = strdup( chan_name );

    g_ca_list_tail->flink = cur;
    g_ca_list_tail = cur;
    g_ca_list_tail->flink = NULL;

    if ( strcmp( forcedValue, "" ) != 0 ) {
      add_force( chan_name, forcedValue );
    }

    stat = db_list_get_next_db( chan_name, replacement_name,
     63, forcedValue, 63, &eof );

  }

  stat = db_list_term();
  if ( !stat ) {
    show_error( SNAP_ERROR, "Error from db_list_term" );
    goto err_return;
  }

  filePostfix = 1;
  while ( 1 ) {

    /* printf( "write file\n" ); */

    stat = out_file_init( filePostfix );
    if ( !( stat & 1 ) ) {
      if ( stat == 0 ) show_error( SNAP_ERROR, "Error from out_file_init" );
      if ( stat == 2 ) show_error( SNAP_ERROR, "Run ID already exists" );
      goto err_return;
    }

    cur = g_ca_list_head->flink;
    while ( cur ) {

      stat = write_channel( cur->id, cur->name );
      if ( !stat ) goto err_return;

      cur = cur->flink;

    }

    stat = out_file_term();
    if ( !stat ) {
      show_error( SNAP_ERROR, "Error from out_file_term" );
      goto err_return;
    }

    if ( ++filePostfix > 3 ) filePostfix = 1;

    epicsThreadSleep( 10.0 );

    g_snap_first_time = 0;

  }

  show_error( SNAP_SUCCESS, "Success" );

  return;

err_return:

  printf( "snap_db_func - abort on error\n" );

}

long epicsShareAPI snap_db (
  char *arg1,
  char *arg2,
  char *arg3
) {

epicsThreadId id;

  if ( arg3 == NULL ) {
    fprintf( stderr,
     "vsnap_db [%-d.%-d.%-d]\n<db list file> <snap file> <node name>\n",
     g_major, g_minor, g_release );
    return 0;
  }

  strncpy( g_arg1, arg1, 255 );
  g_arg1[255] = 0;

  strncpy( g_arg2, arg2, 255 );
  g_arg2[255] = 0;

  strncpy( g_arg3, arg3, 255 );
  g_arg3[255] = 0;

  id = epicsThreadCreate( "snap_db",
   epicsThreadPriorityLow,
   epicsThreadGetStackSize( epicsThreadStackMedium ),
   snap_db_func, NULL );

  return 0;

}
