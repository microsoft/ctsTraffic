# Design and Architecture
This document outlines the design principles and architectural decisions that guide the development of our software system.
It serves as a reference for developers, architects, and stakeholders to understand the structure and rationale behind our design choices.

## Motivation
This solution was developed after we shipping Windows 7, which made it clear we needed tooling that could reliably scale to meet demands of modern networks,
while being flexible enough to adapt to a variety of scenarios. Existing tools were 'single purpose', not reliable enough, nor could they scale beyond their one
target goal.

This we set out to build a new set of network tooling that could meet the following goals:
- **Reliable**: we must have tooling that works consistently across a variety of environments and scenarios.
	- First and foremost, this means proper handling of any and all error conditions - being resilient to network changes.
		- In networking, if something can go wrong, it will go wrong. Tooling must be able to handle these conditions gracefully, reporting transparently.
	- Reliability means more than just "never crashes". Reliable network tooling must produce predictable and accurate results.
		- Tooling must guarantee (and report on) successful delivery of data, or provide clear diagnostics when delivery fails.
- **Scalable**: the tooling must be able to adapt to both network conditions as well as workload demands.
We found that there are 3 major axes we must scale well to be a useful tool.
	- **scale down**: the tool must scale down to run performant against small target environments, such as IoT devices, or small tablets.
	- **scale up**: the tool must scale up to saturate large servers connected to high-speed / high-bandwidth networks with an appropriate workload.
	- **scale out**: the tool must scale out to handle large numbers of network connections, while maintaining performance and reliability.
- **Performant**: the tooling must utilize recommended coding practices to demsonstrate the performance capabilities of the platform.
	- **canonical patterns**: the tool must utilize canonical coding patterns to demonstrate best practices, demonstrating how the Windows and Winsock
platform scales to meet hardware and network demands. It is a **non-goal** to write "performance micro-benchark" code, that is not representative
of real-world usage with real applications.
	- **efficient resource usage**: the tool must utilize system resources efficiently, avoiding unnecessary overhead and contention. This includes
not just memory overhead, but overhead from context switching, lock contention, and other sources of inefficiency. This is especially important 
to be able to scale effectively across the above dimensions.
- **Configurable**: the tooling must be able to be configured to meet a variety of deployments
	- Configuration requirements often required deploying variable numbers of clients communicating with variable numbers of servers.
	- Configuration requirements often required variable numbers of concurrent connections, buffer sizes, and transfer sizes.
	- Configuration requirements often required variable protocol choices: TCP vs UDP, IPv4 vs IPv6.
	- Configuration requirements often required variable protocol behaviors
		- e.g., sending/receiving large amounts of data over long connections
		- e.g., requiring short-lived, low-latency, predictable messaging
	- Configuration requirements would even require different API choices
		- e.g., using traditional blocking sockets vs. overlapped I/O vs. IOCP vs. Winsock Registered I/O (RIO)

With these requirements in mind, we set out to design a new set of tooling that would meet **all** of these goals. One would not need to trade-off
reliability for performance, or scalability for configurability. All IO patterns would meeting the same reliability and scalability requirements,
which meeting the target performance characteristics of the chosen IO pattern.

## Architectural Overview
The architecture of the tooling had to allow for flexibility in the protocols being used, the APIs being used,
and the IO patterns (sending and receiving data). All-the-while naturally scaling up/down/out to meet the demands of
the customer's deployment; while maintaining a very high bar for reliability (we had a zero-bug tolerance).

This led us to design small, individual components that could be composed together to meet different configurations,
while not sacrificing reliability or scalability. Each component would have a well-defined interfaces, allowing
for predictble behavior across any configuration.

We ended up splitting the architecture into the following components:
- **ctsSocketBroker object**: responsible for maintaing the required numbers of connections, and exiting when complete.
	- It is responsible for 'scaling' the connect requests to meet the target number of concurrent connections.
	 	- e.g., when scaling out to thousands of connections, the socket broker would stagger the connect requests to avoid
		protocol failures from timeouts and resource exhaustion.
	- It is responsible for tracking the total number of connections
		- It tracks the number of desired 'concurrently connected' connections (e.g., maintain 10 concurrent connections)
		- It tracks the total number of connections before exiting (e.g., go through 100K total connections)
	- It tracks state for each connection.
	 	- the goal-state for how many connections to be concurrently connected
		- the number of successfully connected sockets ('active')
		- the number of sockets in the process of trying to connect ('pending')
	- It is responsible for 'starting' and 'stopping' the tool.
		- when starting, it creates SocketState objects to represent each connection.
		- when stopping, it gracefully indicates all SocketState objects to close.
- **ctsSocketState object**: responsible for tracking the 'state' of each individual socket
	- It invokes the appropriate API (based off the user-specified configuration) for each stage of a socket's lifecycle
		- *creating* the socket, *connecting* the socket, *initiating IO* over the socket, *closing* the socket
	- It maintains a weak-reference to its parent Socket Broker object, communicating with the parent object when
	connections are connected and closed.
	- It creates the underlying socket object, based off the user-specified configuration
- **ctsSocket object**: tracks the underlying SOCKET handle 
	- It tracks the following properties of the underlying socket
		- the SOCKET handle, with associated locks
		- the local and remote addresses
		- the IO Pattern that will send/recv data over the socket
		- the current IO count pended over the socket
		- the timer if IO needs to be delayed (scheduled in the future) over the socket
		- registers for ISB (ideal-send-backlog) changes, and updates the IO Pattern accordingly
			- *the ISB will tell the IO Pattern the optimal # of bytes to have pended being sent at any point in time*
	- It is responsible for communicating state changes to its parent ctsSocketState object
		- the functors to create/connect/initiate IO/close the socket are provided the ctsSocket object
		- thus the ctsSocket object knows when any operation fails, and can communicate that to the ctsSocketState object
- **ctsIOPattern object**: responsible for tracking what IO should be sent/received over a socket
	- Each IO pattern derives off this base class, such that each derived class only tracks if the next IO request should
	send or receive data, and optionally how many bytes to send/receive.
	- the ctsIOPattern base class tracks all buffers associated with sending and receiving data
		- it uses static buffers for sending data with a known pattern, which can be used to verify data integrity
		on the receiving side
		- it uses static buffers for receiving data when data integrity was indicated not to be verified
		- it manages dynamic buffers for receiving data when data integrity needs to be verified
		- it manages RIO buffers when using the RIO API to send/receive data
	- the ctsIOPattern base class maintains a weak-reference to its parent ctsSocket object
		- this allows the IO pattern object to share the ctsSocket object lock when needed
	- the ctsIOPattern base class is used by API functors to request IO to be sent over a socket,
	and to indicate IO completion.
	- manages statistics for all IO operations

The objects relationships can be visualized as follows.
Note that each child object has a weak-reference to its parent object, allowing for bi-directional communication
```
main: starts and stops the ctsSocketBroker object
|
- ctsSocketBroker: the parent of all objects (maintains a vector of all ctsSocketState objects)
	|
	- ctsSocketState: allocates a single ctsSocket object
		|
		- ctsSocket: allocates a single ctsIOPattern object
			|
			- ctsIOPattern
```

The design allows for easier extensibility, as new APIs and IO patterns can be added without modifying the 'engine'
that tracks the life of each socket.

Different Winsock APIs can be implemented as 'API functors' that are accessed by the ctsSocketState object,
which in turn provides them to the ctsSocket object to invoke as the ctsIOPattern indicates IO to be sent/received.

API functors are created based off user-configuration, allowing for different APIs to be used without modifying the
underlying engine. There are 5 API functor types:
- **Socket Creation functor**: responsible for creating the underlying SOCKET handle
- **Socket Connect functor**: responsible for connecting the underlying SOCKET handle (client-side only)
- **Socket Accept functor**: responsible for accepting incoming connections on the underlying SOCKET handle (server-side only)
- **Socket IO functor**: responsible for sending/receiving data over the underlying SOCKET handle
- **Socket Close functor**: responsible for closing the underlying SOCKET handle

The state -> functor relationships can be visualized as follows
```
	ctsSocketState: tracks the state of a connection to invoke the appropriate functor (handling errors)
		|
		- Socket Creation functor: passes the shared_ptr<ctsSocket> to create the SOCKET
		- Socket Connect functor: passes the shared_ptr<ctsSocket> to connect the SOCKET
		- Socket Accept functor: passes the shared_ptr<ctsSocket> to accept the next connection
		- Socket IO functor: passes the shared_ptr<ctsSocket> to send/receive data
		- Socket Close functor: passes the shared_ptr<ctsSocket> to close the SOCKET
		|
		- ctsSocket
			|
			- updated by each functor it's given
			- creates a ctsIOPattern - accessed from the IO functor
```

Because the ctsSocketState tracks the 'state' of the connection, the ctsSocket tracks the state of the socket,
and the ctsIOPattern tracks the state of the IO being sent/received, functors can effectively be 'stateless': they
can focus on just implementing the API logic itself, without needing to track any state.

## Interface contracts
Each component has a well-defined interface contract, allowing for predictable behavior across any configuration.

### ctsSocketBroker interface contract
- **Start**: invoked by **main** to "start" the tool by creating the required number of ctsSocketState objects to meet the required
number of concurrent connections.
	- ctsSocketBroker will continue to create ctsSocketState objects as connections complete, until
	the total number of connections is met.
- **InitiatingIo**: invoked by a child **ctsSocketState** object when its connection is connected, and is about to initiate IO.
- **Closing**: invoked by a child **ctsSocketState** object when its connection is closing.
- **Wait**: invoked by **main** to wait until all connections are complete (or the user requested to stop the tool).

### ctsSocketState interface contract
- **Start**: invoked by **ctsSocketBroker** to start the state machine for this connection.
- **CompleteState**: invoked by the underlying **ctsSocket** object when an operation completes
	- if the operation was successful, it will advance to the next state
	- if the operation failed, it will transition to the closing state
- **GetCurrentState**: accessor to retrieve the current state of this connection.
	- Used by **ctsSocketBroker** when pruning connections that are completed.

### ctsSocket interface contract
- **SetSocket**: invoked by **ctsSocketState** to assign the object a new SOCKET value and fully initializes the object for use
- **AcquireSocketLock**: invoked by **API functors** when they need to access the SOCKET value
	- provides access to the internal lock that protects the SOCKET value
	- returns an RAII object (SocketReference) that will release the lock when it goes out of scope
- **CloseSocket**: invoked by **ctsSocketState** when the connection is closing
	- optionally invoked by **API functors** when they need to force a RST on the socket
		- *else the socket would be closed with a 4-way FIN*
- **CreateIoPattern**: invoked by **ctsSocketState** after the socket is connected to create the appropriate
	- Creates the ctsIOPattern for this socket
- **CompleteState**: invoked by **API functors** when an operation completes
	- callers are expected to call this when their 'stage' is complete for this SOCKET
- **GetLocalSockaddr/GetLocalSockaddr**: invoked by **API functors** to access/update the local address of this socket
- **GetRemoteSockaddr/SetRemoteSockaddr**: invoked by **API functors** to access/update the remote address of this socket
- **GetIocpThreadpool**: invoked by **API functors** when they need access to the IOCP threadpool associated with this socket
- **IncrementIo/DecrementIo**: invoked by **API functors** to use for ref-counting the # of IO they have issued on this socket
	- this allows the socket to track all pended IO requests, to know when all IO is complete before closing
- **GetPendedIoCount**: invoked by **API functors** to access the current # of pended IO requests on this socket
	- rarely used - currently only needed for one API functor implemeting a UDP server pattern
- **SetTimer**: invoked by **API functors** when they need to schedule a future task to be executed
	- e.g., for delayed sends/receives
- **PrintPatternResults**: called by the **ctsSocketState** parent to print the connection data after closing

### ctsIOPattern interface contract
- **MakeIoPattern**: invoked by **ctsSocket** when the socket is connected to create the appropriate IO pattern
	- static factory method to create the appropriate IO pattern based off user-configuration
- **AccessSharedBuffer**: invoked by **unit tests** to access the shared send/recv buffer
	- static method to access the shared send/recv buffer
- **SetParent**: invoked by **ctsSocket** to assign the parent ctsSocket object
- **PrintStatistics**: invoked by **ctsSocket** after the socket is closed
	- implemented by ctsIoPatternStatistics to print pattern-specific statistics
- **PrintTcpInfo**: invoked by **ctsSocket** after the socket is closed
	- implemented by ctsIoPatternStatistics to print TCP_INFO for the socket
- **SetIdealSendBacklog**: invoked by **ctsSocket** to assign the ideal send backlog for the socket
	- this allows the IO pattern to adjust the pended bytes sent based off network conditions
- **InitiateIo**: invoked by **API functors** to retrieve the next IO task to perform on this socket
	- returns a ctsTask object indicating the next IO operation to perform
- **CompleteIo**: invoked by **API functors** to indicate the completion of a prior requested IO operation
	- accepts the ctsTask object provided by **InitiateIo**, the # of bytes transferred, and the status code of the operation
- **RegisterCallback**: invoked by **API functors** to register a callback to be invoked when the functor should
send/receive IO on that socket as dictated by the IO pattern.
	- used by the UDP client to resend START messages if no response was received from the server
	- used by the UDP client to send ABORT messages to the server on hard-errors
- **GetLastPatternError**: invoked by **ctsSocket** and **API functors** to access the last error encountered by the IO pattern
- **GetRioBufferIdCount**: invoked by the **API functor** implementing RIO to determine the maximum number of IO operations
	- used to pre-allocate vectors for tracking

### API Functor interface contract
Each API functor is given a shared_ptr<ctsSocket> to operate on and is expected to call CompleteState() on the ctsSocket object
when their operation is complete.

For create, connect, accept, and close functors, these are the only interactions needed: they make the appropriate API calls,
then call CompleteState() when done.

For IO functors, they will need to interact with both the ctsSocket and the ctsIOPattern object to determine what IO to perform next.
IO functors will generally follow this pattern:

```c++
    std::weak_ptr<ctsSocket>& weakSocket; // provided to the functor

    // first get a reference to the socket
    const auto sharedSocket(weakSocket.lock());
    if (!sharedSocket)
    {
        return;
    }

    // next, get a reference to the IO pattern created for this socket
    // note that this requires acquiring the socket lock
    const auto lockedSocket = sharedSocket->AcquireSocketLock();
    const auto lockedPattern = lockedSocket.GetPattern();
    if (!lockedPattern)
    {
        return;
    }

    // next, get the underlying SOCKET handle
    SOCKET socket = lockedSocket.GetSocket();
    if (socket != INVALID_SOCKET)
    {
        auto ioDone = false;
        // loop until failure or initiate_io returns None
        while (!ioDone && NO_ERROR == ioError)
        {
            // each loop requests the next ctsTask
            // NOTE: the ctsTask is how the IO pattern indicates what IO to perform next

            ctsTask nextIo = lockedPattern->InitiateIo();
            if (ctsTaskAction::None == nextIo.m_ioAction)
            {
                // nothing failed, just no more IO right now
                ioDone = true;
                continue;
            }

            if (ctsTaskAction::GracefulShutdown == nextIo.m_ioAction)
            {
                // this action indicates we need to perform a graceful shutdown
                if (0 != shutdown(socket, SD_SEND))
                {
                    ioError = WSAGetLastError();
                }

                // always call CompleteIo after performing the action
                ioDone = lockedPattern->CompleteIo(nextIo, 0, ioError) != ctsIoStatus::ContinueIo;
                continue;
            }

            if (ctsTaskAction::HardShutdown == nextIo.m_ioAction)
            {
                // this action indicates we need to perform a hard shutdown (i.e., RST)
                // pass through -1 to force an RST with the closesocket
                ioError = sharedSocket->CloseSocket(static_cast<uint32_t>(SOCKET_ERROR));
                socket = INVALID_SOCKET;

                ioDone = lockedPattern->CompleteIo(nextIo, 0, ioError) != ctsIoStatus::ContinueIo;
                continue;
            }

            // else we need to initiate another IO
            // add-ref the IO about to start
            ioCount = sharedSocket->IncrementIo();

            // make the appropriate API call to send or receive data
            // can call sharedSocket->GetIocpThreadpool() to get the IOCP threadpool if needed

            // get the buffer to send or receive from the returned ctsTask
            char* ioBuffer = nextIo.m_buffer + nextIo.m_bufferOffset;
            const auto ioBufferLength = nextIo.m_bufferLength;
            if (ctsTaskAction::Send == nextIo.m_ioAction)
            {
                // call the appropriate send API with socket, ioBuffer, and ioBufferLength
            }
            else
            {
                // call the appropriate recv API with socket, ioBuffer, and ioBufferLength
            }

            // if the call is pended, continue the loop to request the next IO

            // if the call succeeded inline, or failed, we need to call CompleteIo and decrement the IO count
            if () // call succeeded inline, or failed
            {
                ioCount = sharedSocket->DecrementIo();

                // call back to the socket to inform it that the call failed to see if it wants to request more IO
                switch (const ctsIoStatus protocolStatus = lockedPattern->CompleteIo(nextIo, 0, ioError))
                {
                    case ctsIoStatus::ContinueIo:
                        // the protocol wants to ignore the error and send more data
                        ioError = NO_ERROR;
                        ioDone = false;
                        break;

                    case ctsIoStatus::CompletedIo:
                        // the protocol wants to ignore the error but is done with IO
                        ioError = NO_ERROR;
                        ioDone = true;
                        break;

                    case ctsIoStatus::FailedIo:
                        // the protocol acknowledged the failure - socket is done with IO
                        ioError = static_cast<int>(lockedPattern->GetLastPatternError());
                        ioDone = true;
                        break;
                }
            }
        }
    }
    else
    {
        ioError = WSAECONNABORTED;
    }

    if (0 == ioCount)
    {
        // complete the ctsSocket if we have no IO pended for this socket
        sharedSocket->CompleteState(ioError);
    }
```

