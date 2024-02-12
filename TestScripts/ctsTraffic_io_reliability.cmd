@echo off

REM 
REM 
REM Copyright (c) Microsoft Corporation
REM All rights reserved.
REM 
REM Licensed under the Apache License, Version 2.0 (the ""License""); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
REM 
REM THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
REM 
REM See the Apache Version 2.0 License for specific language governing permissions and limitations under the License.
REM 
REM

echo ----------------------------------------------------------------------------------------------------------------------------
echo ----------------------------------------------------------------------------------------------------------------------------
echo This validation script will verify handling TCP connection states at scale (1000 concurrent connections being closed or RST randomly)
echo .
echo Having connections randomly terminated locally and remotely has been found to be an effective way to find reliability issues in drivers and filters.
echo .
echo This can be run on any number of clients targeting the same server (or targeting multiple servers, if desired).
echo .
echo I would recommend a minimum of 1000 concurrent connections to focus on stressing handling connection failures.
echo  ... but feel free to modify -Connections as needed for targeting specific scenarios
echo .
echo Note: this sets the Transfer size to be randomly chosen with each connection (-Transfer:[0xff,0xffffff])
echo  ... random transfer size will naturally cause either the client or the server to closesocket() while IO is in flight
echo .
echo Note: this is set to run for 1,800,000 ms (30 minutes) - this can be modified to match your target goals
echo .
echo This script expects there to be many NetError's and DataError's - we are intentionally injecting failures in all connections
echo ----------------------------------------------------------------------------------------------------------------------------
echo ----------------------------------------------------------------------------------------------------------------------------

if '%1' == '' (
  echo Must specify server or client as the first argument
  goto :exit
)

if /i '%1' NEQ 'server' (
  if /i '%1' NEQ 'client' (
    echo Must specify server or client
    goto :exit
  )

  if '%2' == '' (
    echo client must specify the target server name or IP address
    goto :exit
  )
)


set TimeToRun=1800000
set Options= -Protocol:tcp -Pattern:duplex -Verify:connection -Transfer:[0xff,0xffffff]

if '%1' == 'server' (
  ctsTraffic.exe -Listen:* %Options% -ConsoleVerbosity:1
)

if '%1' == 'client' (
   ctsTraffic.exe -Target:%2 %Options% -Shutdown:random -Connections:1000 -ConsoleVerbosity:1 -TimeLimit:%TimeToRun%
)

:exit