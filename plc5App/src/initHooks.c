/* initHooks.c	ioc initialization hooks */ 
/* share/src/db @(#)initHooks.c	1.5     7/11/94 */
/*
 *      Author:		Marty Kraimer
 *      Date:		06-01-91
 *
 *      Experimental Physics and Industrial Control System (EPICS)
 *
 *      Copyright 1991, the Regents of the University of California,
 *      and the University of Chicago Board of Governors.
 *
 *      This software was produced under  U.S. Government contracts:
 *      (W-7405-ENG-36) at the Los Alamos National Laboratory,
 *      and (W-31-109-ENG-38) at Argonne National Laboratory.
 *
 *      Initial development by:
 *              The Controls and Automation Group (AT-8)
 *              Ground Test Accelerator
 *              Accelerator Technology Division
 *              Los Alamos National Laboratory
 *
 *      Co-developed with
 *              The Controls and Computing Group
 *              Accelerator Systems Division
 *              Advanced Photon Source
 *              Argonne National Laboratory
 *
 * Modification Log:
 * -----------------
 * .01  09-05-92	rcz	initial version
 * .02  09-10-92	rcz	changed return from void to long
 * .03  09-10-92	rcz	changed completely
 * .04  09-10-92	rcz	bug - moved call to setMasterTimeToSelf later
 *
 */


#include	<stdio.h>
#include	<initHooks.h>
#include	<epicsPrint.h>
#include        <iocsh.h>
#include        <epicsExport.h>


extern void notifyPLC5 ( void );

/*
 * INITHOOKS
 *
 * called by iocInit at various points during initialization
 *
 */


/* If this function (initHooks) is loaded, iocInit calls this function
 * at certain defined points during IOC initialization */
static void stdInitHooks(initHookState state)
{
	int i;

	switch (state) {
	case initHookAtBeginning :
	    break;
	case initHookAfterCallbackInit :
	    break;
	case initHookAfterCaLinkInit :
	    break;
	case initHookAfterInitDrvSup :
	    break;
	case initHookAfterInitRecSup :
	    break;
	case initHookAfterInitDevSup :
	    break;
	case initHookAfterInitDatabase :
	    break;
	case initHookAfterFinishDevSup :
	    break;
	case initHookAfterScanInit :
	    break;
	case initHookAfterInterruptAccept :
	    break;
	case initHookAfterInitialProcess :
	    break;
	case initHookAtEnd :
	  notifyPLC5();
	    break;
	default:
	    break;
	}
	return;
}

void stdInitHooksRegister(void)
{
   initHookRegister(stdInitHooks);
}

epicsExportRegistrar(stdInitHooksRegister);
