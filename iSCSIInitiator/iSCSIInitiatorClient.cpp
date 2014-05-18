/**
 * @author		Nareg Sinenian
 * @file		iSCSIInitiatorClient.cpp
 * @date		October 13, 2013
 * @version		1.0
 * @copyright	(c) 2013 Nareg Sinenian. All rights reserved.
 */

#include "iSCSIVirtualHBA.h"
#include "iSCSIInitiatorClient.h"
#include <IOKit/IOLib.h>

/** Required IOKit macro that defines the constructors, destructors, etc. */
OSDefineMetaClassAndStructors(iSCSIInitiatorClient,IOUserClient);

/** The superclass is defined as a macro to follow IOKit conventions. */
#define super IOUserClient

/** Array of methods that can be called by user-space. */
const IOExternalMethodDispatch iSCSIInitiatorClient::methods[kiSCSIInitiatorNumMethods] = {
	{
		(IOExternalMethodAction) &iSCSIInitiatorClient::OpenInitiator,
		0, // Scalar input count
		0, // Structure input size
		0, // Scalar output count
		0  // Structure output size
	},
	{
		(IOExternalMethodAction) &iSCSIInitiatorClient::CloseInitiator,
		0,
		0,
        0,
		0
	},
	{
		(IOExternalMethodAction) &iSCSIInitiatorClient::CreateSession,
		0,
		0,
		1, // Return value for SessionCreate
		0
	},
    {
		(IOExternalMethodAction) &iSCSIInitiatorClient::ReleaseSession,
		1, // Parameter for SessionRelease
		0,
		0,
		0
	},
    {
		(IOExternalMethodAction) &iSCSIInitiatorClient::SetSessionOptions,
		1, // Parameter for SessionRelease
        sizeof(iSCSISessionOptions),
		0,
		0
	},
    {
		(IOExternalMethodAction) &iSCSIInitiatorClient::GetSessionOptions,
		1, // Parameter for SessionRelease
		0,
		0,
		sizeof(iSCSISessionOptions)
	},
	{
		(IOExternalMethodAction) &iSCSIInitiatorClient::CreateConnection,
		2,                          // Session, domain
		2*sizeof(struct sockaddr),  // Address structures
		2,                          // Return value for CreateConnection
		0
	},
	{
		(IOExternalMethodAction) &iSCSIInitiatorClient::ReleaseConnection,
		2, // Session Id, connection Id
		0,
		0,
		0
	},
    {
		(IOExternalMethodAction) &iSCSIInitiatorClient::ActivateConnection,
		2, // Session Id, connection Id
		0,
		1,
		0
	},
	{
		(IOExternalMethodAction) &iSCSIInitiatorClient::DeactivateConnection,
		2, // Session Id, connection Id
		0,
		1,
		0
	},
	{
		(IOExternalMethodAction) &iSCSIInitiatorClient::SendBHS,
		0,                                  // Session Id, connection Id
		sizeof(struct __iSCSIPDUCommonBHS), // Buffer to send
		0,                                  // Return value
		0
	},
	{
		(IOExternalMethodAction) &iSCSIInitiatorClient::SendData,
        2,                                  // Session Id, connection Id
		kIOUCVariableStructureSize,         // Data is a variable size block
		1,                                  // Return value
		0
	},
    {
		(IOExternalMethodAction) &iSCSIInitiatorClient::RecvBHS,
        2,                                  // Session Id, connection Id
		0,
		1,                                  // Return value
		sizeof(struct __iSCSIPDUCommonBHS), // Receive buffer
	},
    {
		(IOExternalMethodAction) &iSCSIInitiatorClient::RecvData,
        2,                                  // Session Id, connection Id
		0,
		1,                                  // Return value
		kIOUCVariableStructureSize          // Receive buffer
	},
    {
		(IOExternalMethodAction) &iSCSIInitiatorClient::SetConnectionOptions,
		2, // Session Id, connection Id
        sizeof(iSCSIConnectionOptions),
		0,
		0
	},
    {
		(IOExternalMethodAction) &iSCSIInitiatorClient::GetConnectionOptions,
		2, // Session Id, connection Id
		0,
		0,
		sizeof(iSCSIConnectionOptions)
	},
    {
		(IOExternalMethodAction) &iSCSIInitiatorClient::GetActiveConnection,
		1, // Session Id
		0,
		1, // Connection Id
        0
	}
};

IOReturn iSCSIInitiatorClient::externalMethod(uint32_t selector,
											  IOExternalMethodArguments * args,
											  IOExternalMethodDispatch * dispatch,
											  OSObject * target,
											  void * ref)
{
	// Sanity check the selector
	if(selector >= kiSCSIInitiatorNumMethods)
		return kIOReturnUnsupported;
	
	
	// Call the appropriate function for the current instance of the class
	return super::externalMethod(selector,
								 args,
								 (IOExternalMethodDispatch *)&iSCSIInitiatorClient::methods[selector],
								 this,
								 ref);
}


// Called as a result of user-space call to IOServiceOpen()
bool iSCSIInitiatorClient::initWithTask(task_t owningTask,
										void * securityToken,
										UInt32 type,
										OSDictionary * properties)
{
	// Save owning task, securty token and type so that we can validate user
	// as a root user (UID 0) for secure operations (e.g., adding an iSCSI
	// target requires privileges).
	this->owningTask = owningTask;
	this->securityToken = securityToken;
	this->type = type;
        
	// Perform any initialization tasks here
	return super::initWithTask(owningTask,securityToken,type,properties);
}

//Called after initWithTask as a result of call to IOServiceOpen()
bool iSCSIInitiatorClient::start(IOService * provider)
{
	// Check to ensure that the provider is actually an iSCSI initiator
	if((this->provider = OSDynamicCast(iSCSIVirtualHBA,provider)) == NULL)
		return false;

	return super::start(provider);
}

void iSCSIInitiatorClient::stop(IOService * provider)
{
	super::stop(provider);
}

// Called as a result of user-space call to IOServiceClose()
IOReturn iSCSIInitiatorClient::clientClose()
{
    // Tell HBA to release any resources that aren't active (e.g.,
    // connections we started to establish but didn't activate)
    
	// Ensure that the connection has been closed (in case the user calls
	// IOServiceClose() before calling our close() method
	close();
    
	
	// Terminate ourselves
	terminate();
	
	return kIOReturnSuccess;
}

// Called if the user-space client is terminated without calling
// IOServiceClose() or close()
IOReturn iSCSIInitiatorClient::clientDied()
{
    // Tell HBA to release any resources that aren't active (e.g.,
    // connections we started to establish but didn't activate)
    
    
    // Close the provider (decrease retain count)
    close();
    
	return super::clientDied();
}

// Invoked from user space remotely by calling iSCSIInitiatorOpen()
IOReturn iSCSIInitiatorClient::open()
{
	// Ensure that we are attached to a provider
	if(isInactive() || provider == NULL)
		return kIOReturnNotAttached;
	
	// Open the provider (iSCSIInitiator) for this this client
	if(provider->open(this))
		return kIOReturnSuccess;
    
	// At this point we couldn't open the client for the provider for some
	// other reason
	return kIOReturnNotOpen;
}

// Invoked from user space remotely by calling iSCSIInitiatorClose()
IOReturn iSCSIInitiatorClient::close()
{
	// If we're not active or have no provider we're not attached
	if(isInactive() || provider == NULL)
		return kIOReturnNotAttached;
	
	// If the provider isn't open for us then return not open
	else if(!provider->isOpen(this))
		return kIOReturnNotOpen;

	// At this point we're attached & open, close the connection
	provider->close(this);
	
	return kIOReturnSuccess;
}

/** Dispatched function called from the device interface to this user
 *	client .*/
IOReturn iSCSIInitiatorClient::OpenInitiator(iSCSIInitiatorClient * target,
                                             void * reference,
                                             IOExternalMethodArguments * args)
{
    return target->open();
}

/** Dispatched function called from the device interface to this user
 *	client .*/
IOReturn iSCSIInitiatorClient::CloseInitiator(iSCSIInitiatorClient * target,
                                              void * reference,
                                              IOExternalMethodArguments * args)
{
    return target->close();
}

/** Dispatched function invoked from user-space to create new session. */
IOReturn iSCSIInitiatorClient::CreateSession(iSCSIInitiatorClient * target,
                                             void * reference,
                                             IOExternalMethodArguments * args)
{
    // Create a new session and return session ID
    args->scalarOutputCount = 1;
    *args->scalarOutput = target->provider->CreateSession();

    return kIOReturnSuccess;
}

/** Dispatched function invoked from user-space to release session. */
IOReturn iSCSIInitiatorClient::ReleaseSession(iSCSIInitiatorClient * target,
                                              void * reference,
                                              IOExternalMethodArguments * args)
{
    // Release the session with the specified ID
    target->provider->ReleaseSession(*args->scalarInput);
    return kIOReturnSuccess;
}

IOReturn iSCSIInitiatorClient::SetSessionOptions(iSCSIInitiatorClient * target,
                                                 void * reference,
                                                 IOExternalMethodArguments * args)
{
    if(target->provider->SetSessionOptions(
            (UInt64)args->scalarInput[0],                       // Session qualifier
            (iSCSISessionOptions*)args->structureInput) == 0)   // Options
        return kIOReturnSuccess;
    
    return kIOReturnError;
}

IOReturn iSCSIInitiatorClient::GetSessionOptions(iSCSIInitiatorClient * target,
                                                 void * reference,
                                                 IOExternalMethodArguments * args)
{
    if(args->structureOutputSize < sizeof(iSCSIConnectionOptions))
        return kIOReturnMessageTooLarge;
    
    if(target->provider->GetSessionOptions(
        (UInt16)args->scalarInput[0],
        (iSCSISessionOptions *)args->structureOutput) == 0)
        return kIOReturnSuccess;
    
    return kIOReturnError;
}

/** Dispatched function invoked from user-space to create new connection. */
IOReturn iSCSIInitiatorClient::CreateConnection(iSCSIInitiatorClient * target,
                                                void * reference,
                                                IOExternalMethodArguments * args)
{
    UInt32 connectionId;
    
    // Create a connection
    args->scalarOutput[0] = target->provider->CreateConnection(
            (UInt16)args->scalarInput[0],       // Session qualifier
            (int)args->scalarInput[1],          // Socket domain
            (const sockaddr *)args->structureInput,
            (const sockaddr *)args->structureInput+args->structureInputSize,
            &connectionId);
    
    args->scalarOutput[1] = connectionId;
    args->scalarOutputCount = 2;

    return kIOReturnSuccess;
}

/** Dispatched function invoked from user-space to release connection. */
IOReturn iSCSIInitiatorClient::ReleaseConnection(iSCSIInitiatorClient * target,
                                                 void * reference,
                                                 IOExternalMethodArguments * args)
{
    target->provider->ReleaseConnection((UInt16)args->scalarInput[0],
                                        (UInt32)args->scalarInput[1]);
    return kIOReturnSuccess;
}

IOReturn iSCSIInitiatorClient::ActivateConnection(iSCSIInitiatorClient * target,
                                                  void * reference,
                                                  IOExternalMethodArguments * args)
{
    *args->scalarOutput =
        target->provider->ActivateConnection((UInt16)args->scalarInput[0],
                                             (UInt32)args->scalarInput[1]);
    return kIOReturnSuccess;
}

IOReturn iSCSIInitiatorClient::DeactivateConnection(iSCSIInitiatorClient * target,
                                                    void * reference,
                                                    IOExternalMethodArguments * args)
{
    *args->scalarOutput =
    target->provider->DeactivateConnection((UInt16)args->scalarInput[0],
                                           (UInt32)args->scalarInput[1]);

    return kIOReturnSuccess;
}


/** Dispatched function invoked from user-space to send data
 *  over an existing, active connection. */
IOReturn iSCSIInitiatorClient::SendBHS(iSCSIInitiatorClient * target,
                                       void * reference,
                                       IOExternalMethodArguments * args)
{
    // Validate input
    if(args->structureInputSize != kiSCSIPDUBasicHeaderSegmentSize)
        return kIOReturnNoSpace;
    
   // target->bhsBuffer = *(iSCSIPDUInitiatorBHS*)args->structureInput;
    
    memcpy(&target->bhsBuffer,args->structureInput,kiSCSIPDUBasicHeaderSegmentSize);
    
    return kIOReturnSuccess;
}

/** Dispatched function invoked from user-space to send data
 *  over an existing, active connection. */
IOReturn iSCSIInitiatorClient::SendData(iSCSIInitiatorClient * target,
                                        void * reference,
                                        IOExternalMethodArguments * args)
{
    // Send data and return the result
    args->scalarOutputCount = 1;
    
    *args->scalarOutput = target->provider->SendPDUUser(
        (UInt16)args->scalarInput[0],                   // Session qualifier
        (UInt32)args->scalarInput[1],                   // Connection ID
        &(target->bhsBuffer),                           // BHS to send
        (void*)args->structureInput,                    // Data to send
        args->structureInputSize);                      // Size of data
    
    return kIOReturnSuccess;

}

/** Dispatched function invoked from user-space to receive data
 *  over an existing, active connection, and to retrieve the size of
 *  a user-space buffer that is required to hold the data. */
IOReturn iSCSIInitiatorClient::RecvBHS(iSCSIInitiatorClient * target,
                                        void * reference,
                                        IOExternalMethodArguments * args)
{
    // Receive data and return the result
    args->scalarOutputCount = 1;
    
    if(args->structureOutputSize != kiSCSIPDUBasicHeaderSegmentSize)
        return kIOReturnNoSpace;
    
    *args->scalarOutput = target->provider->RecvPDUHeaderUser(
        (UInt16)args->scalarInput[0],                   // Session qualifier
        (UInt32)args->scalarInput[1],                   // Connection ID
        (iSCSIPDUTargetBHS *)args->structureOutput);    // Data to receive
    
    return kIOReturnSuccess;
}

/** Dispatched function invoked from user-space to receive data
 *  over an existing, active connection, and to retrieve the size of
 *  a user-space buffer that is required to hold the data. */
IOReturn iSCSIInitiatorClient::RecvData(iSCSIInitiatorClient * target,
                                        void * reference,
                                        IOExternalMethodArguments * args)
{
    // Send data and return the result
    args->scalarOutputCount = 1;
    
    *args->scalarOutput = target->provider->RecvPDUDataUser(
        (UInt16)args->scalarInput[0],                   // Session qualifier
        (UInt32)args->scalarInput[1],                   // Connection ID
        (void *)(args->structureOutput),                // Data to receive
        args->structureOutputSize);
                           
    return kIOReturnSuccess;
}

IOReturn iSCSIInitiatorClient::SetConnectionOptions(iSCSIInitiatorClient * target,
                                                    void * reference,
                                                    IOExternalMethodArguments * args)
{
    if(target->provider->SetConnectionOptions(
        (UInt16)args->scalarInput[0],                          // Session qualifier
        (UInt32)args->scalarInput[1],                          // Connection ID
        (iSCSIConnectionOptions*)args->structureInput) == 0)   // Options
        return kIOReturnSuccess;
    
    return kIOReturnError;
}

IOReturn iSCSIInitiatorClient::GetConnectionOptions(iSCSIInitiatorClient * target,
                                                    void * reference,
                                                    IOExternalMethodArguments * args)
{
    if(args->structureOutputSize < sizeof(iSCSIConnectionOptions))
        return kIOReturnMessageTooLarge;
    
    if(target->provider->GetConnectionOptions(
        (UInt16)args->scalarInput[0],
        (UInt32)args->scalarInput[1],
        (iSCSIConnectionOptions *)args->structureOutput) == 0)
        return kIOReturnSuccess;
    
    return kIOReturnError;
}

IOReturn iSCSIInitiatorClient::GetActiveConnection(iSCSIInitiatorClient * target,
                                                   void * reference,
                                                   IOExternalMethodArguments * args)
{
    
    
    
    return kIOReturnError;
}

