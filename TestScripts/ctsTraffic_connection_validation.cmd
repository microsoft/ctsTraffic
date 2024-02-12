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
echo This validation script will verify TCP connection reliability at scale (hundreds, to thousands, of connections/second)
echo Note: the -PortScalability:on option requires ctsTraffic version 2.0.3.3
echo .
echo This can be run on any number of clients targeting the same server (or targeting multiple servers, if desired).
echo .
echo Because this will be cycling through connections very quickly, I would recommend a lower connection count, such as 15 (what I defaulted to below)
echo  ... but feel free to modify -Connections as needed for targeting specific scenarios
echo .
echo Note: this is set to run for 1,800,000 ms (30 minutes) - this can be modified to match your target goals
echo .
echo This script expects there to be almost no errors under NetError's or DataError's
echo  ... there should be zero errors on the server
echo  ... there may be a few bind failed (10048) errors on the client, as we are rapid cycling through a range of local ports
echo  ... but these should be fairly rare
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
set Options= -Protocol:tcp -Pattern:duplex -Verify:data -Transfer:0xff

if '%1' == 'server' (
  ctsTraffic.exe -Listen:* %Options% -ConsoleVerbosity:1
)

if '%1' == 'client' (
   ctsTraffic.exe -Target:%2 %Options% -localport:[12345,12400] -Shutdown:rude -PortScalability:on -Connections:15 -ConsoleVerbosity:2 -TimeLimit:%TimeToRun%
)

:exit