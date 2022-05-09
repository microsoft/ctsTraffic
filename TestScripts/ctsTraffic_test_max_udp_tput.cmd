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

REM
REM on my Ryzen 7 5800X, this will average 200Gbps with between 0.15% and 0.20% drop rate
REM

@echo off

if '%1' == '' (
  echo Must specify server or client as the first argument
  goto :exit
)
if /i '%1' NEQ 'server' (
  if /i '%1' NEQ 'client' (
    echo Must specify server or client
    goto :exit
  )
)

set Role=%1

set Options= -protocol:udp -bitspersecond:1000000000 -FrameRate:100 -StreamLength:30 -ConsoleVerbosity:0 

if '%Role%' == 'server' (
  ctsTraffic.exe -listen:* %Options% -ServerExitLimit:200 -PauseAtEnd:3000
  ctsTraffic.exe -listen:* %Options% -ServerExitLimit:200 -PauseAtEnd:3000
  ctsTraffic.exe -listen:* %Options% -ServerExitLimit:200 -PauseAtEnd:3000
  ctsTraffic.exe -listen:* %Options% -ServerExitLimit:200 -PauseAtEnd:3000
  ctsTraffic.exe -listen:* %Options% -ServerExitLimit:200 -PauseAtEnd:3000
  ctsTraffic.exe -listen:* %Options% -ServerExitLimit:200 -PauseAtEnd:3000
  ctsTraffic.exe -listen:* %Options% -ServerExitLimit:200 -PauseAtEnd:3000
  ctsTraffic.exe -listen:* %Options% -ServerExitLimit:200 -PauseAtEnd:3000
  ctsTraffic.exe -listen:* %Options% -ServerExitLimit:200 -PauseAtEnd:3000
  ctsTraffic.exe -listen:* %Options% -ServerExitLimit:200 -PauseAtEnd:3000
)
if '%Role%' == 'client' (
   ctsTraffic.exe -target:localhost %Options% -BufferDepth:2 -connections:200 -iterations:1 -verify:connection -PauseAtEnd:5000
   ctsTraffic.exe -target:localhost %Options% -BufferDepth:2 -connections:200 -iterations:1 -verify:connection -PauseAtEnd:5000
   ctsTraffic.exe -target:localhost %Options% -BufferDepth:2 -connections:200 -iterations:1 -verify:connection -PauseAtEnd:5000
   ctsTraffic.exe -target:localhost %Options% -BufferDepth:2 -connections:200 -iterations:1 -verify:connection -PauseAtEnd:5000
   ctsTraffic.exe -target:localhost %Options% -BufferDepth:2 -connections:200 -iterations:1 -verify:connection -PauseAtEnd:5000
   ctsTraffic.exe -target:localhost %Options% -BufferDepth:2 -connections:200 -iterations:1 -verify:connection -PauseAtEnd:5000
   ctsTraffic.exe -target:localhost %Options% -BufferDepth:2 -connections:200 -iterations:1 -verify:connection -PauseAtEnd:5000
   ctsTraffic.exe -target:localhost %Options% -BufferDepth:2 -connections:200 -iterations:1 -verify:connection -PauseAtEnd:5000
   ctsTraffic.exe -target:localhost %Options% -BufferDepth:2 -connections:200 -iterations:1 -verify:connection -PauseAtEnd:5000
   ctsTraffic.exe -target:localhost %Options% -BufferDepth:2 -connections:200 -iterations:1 -verify:connection -PauseAtEnd:5000
)

:exit