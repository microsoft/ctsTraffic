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

echo ----------------------------------------------------------------------------------------------------------------------------------------
echo ----------------------------------------------------------------------------------------------------------------------------------------
echo This validation script will verify TCP data and connection integrity at scale (hundreds of concurrent connections) at line rate
echo This will also randomly have client connections terminate with an RST, or a full 4-way FIN (both are perfectly fine ways to terminate a connection)
echo  ... note this -Shutdown:random option requires ctsTraffic version 2.0.3.3
echo .
echo Full-duplex with data validation has been found to be an effective way to find IO issues in drivers and filters.
echo .
echo This can be run on any number of clients targeting the same server (or targeting multiple servers, if desired).
echo .
echo Note: that this is defaulting to a relatively small transfer-size per connection
echo  ... ctsTraffic default is 0x40000000 - 1TB, here I chose 0x10000000 - 256MB
echo  ... this can be modified as needed to match a targeted scenario
echo .
echo I would recommend a minimum of 200 concurrent connections to push the system to maximize data and connection integrity tests.
echo  ... but feel free to modify -Connections as needed for targeting specific scenarios
echo .
echo Note: this is set to run for 1,800,000 ms (30 minutes) - this can be modified to match your target goals
echo .
echo This script expects there to be *zero* NetError's or DataError's - every connection should completed successfully (the Completed column)
echo .
echo ----------------------------------------------------------------------------------------------------------------------------------------
echo ----------------------------------------------------------------------------------------------------------------------------------------

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
set Options= -Protocol:tcp -Pattern:duplex -Verify:data -Transfer:0x10000000

if '%1' == 'server' (
  ctsTraffic.exe -Listen:* %Options% -ConsoleVerbosity:1
)

if '%1' == 'client' (
   ctsTraffic.exe -Target:%2 %Options% -Shutdown:random -Connections:200 -ConsoleVerbosity:2 -TimeLimit:%TimeToRun%
)

:exit