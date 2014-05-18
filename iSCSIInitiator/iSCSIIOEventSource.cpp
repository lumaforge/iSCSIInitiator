/**
 * @author		Nareg Sinenian
 * @file		iSCSIIOEventSource.cpp
 * @date		October 13, 2013
 * @version		1.0
 * @copyright	(c) 2013 Nareg Sinenian. All rights reserved.
 */

#include "iSCSIIOEventSource.h"
#include <sys/ioctl.h>
#include "iSCSIVirtualHBA.h"

#define super IOEventSource

OSDefineMetaClassAndStructors(iSCSIIOEventSource,IOEventSource);

bool iSCSIIOEventSource::init(iSCSIVirtualHBA * owner,
                              iSCSIIOEventSource::Action action,
                              iSCSIVirtualHBA::iSCSISession * session,
                              iSCSIVirtualHBA::iSCSIConnection * connection)
{
	// Initialize superclass, check validity and store socket handle
	if(super::init(owner,(IOEventSource::Action) action))
	{
        iSCSIIOEventSource::session = session;
        iSCSIIOEventSource::connection = connection;
		return true;
	}
	return false;
}

void iSCSIIOEventSource::socketCallback(socket_t so,
										iSCSIIOEventSource * eventSource,
										int waitf)
{
	// Wake up the workloop thread that this event source is attached to.
	// The workloop thread will call checkForWork(), which will then dispatch
	// the action method to process data on the correct socket.
    if(eventSource)
        eventSource->signalWorkAvailable();
}

bool iSCSIIOEventSource::checkForWork()
{
    // First check to ensure that the reason we've been called is because
    // actual data is available at the port (as opposed to other socket events
    iSCSIVirtualHBA * hba = (iSCSIVirtualHBA*)owner;
    
    if(hba->isPDUAvailable(connection))
    {
        // Validate action & owner, then call action on our owner & pass in socket
        if(action && owner)
            (*action)(owner,session,connection);
    }
    
    // Tell workloop thread to call us again (gives it a chance to handle
    // other requests first)
    if(hba->isPDUAvailable(connection))
        return true;
    
    // Tell workloop thread not to call us again until we signal again...
	return false;
}