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

@echo off

REM
REM Known issues:
REM CommandLine: ctsTraffic.exe  -listen:* -buffer:101 -transfer:1000000 -ServerExitLimit:100 -pattern:pull -io:rioiocp -verify:connection
REM - random failures: >> RIOReceive failed (10053)  An established connection was aborted by the software in your host machine.
REM
REM
REM CommandLine: ctsTraffic.exe  -target:localhost -buffer:100 -transfer:1000000 -connections:100 -iterations:1 -pattern:push -io:rioiocp -verify:connection
REM - random failures: >> RIOReceive failed (10053)  An established connection was aborted by the software in your host machine.
REM

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

set CONNECTIONS=100
if /i '%2' == 'debug' (
  set CONNECTIONS=1
)

set Role=%1

CALL :TESTCASES push iocp
CALL :TESTCASES pull iocp
CALL :TESTCASES pushpull iocp
CALL :TESTCASES duplex iocp

CALL :TESTCASES push rioiocp
CALL :TESTCASES pull rioiocp
CALL :TESTCASES pushpull rioiocp
CALL :TESTCASES duplex rioiocp

goto :eof

:TESTCASES
set ServerOptions= -listen:* -buffer:101 -transfer:1000000 -ServerExitLimit:%CONNECTIONS%
set ClientOptions= -target:localhost -buffer:100 -transfer:1000000 -connections:%CONNECTIONS% -iterations:1

REM **********************************************************************************************
REM Test : verify:connection
REM **********************************************************************************************
Set ERRORLEVEL=
if '%Role%' == 'server' (
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:connection
)
if '%Role%' == 'client' (
   REM delay the client
   ping localhost -n 5 > nul
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:connection
)

IF ERRORLEVEL 1 (
  echo TEST FAILED: this test is expected to succeed : %ERRORLEVEL%
  PAUSE
) else (
  echo PASSED
)


REM **********************************************************************************************
REM Test : verify:connection with multiple duplex IO requests
REM **********************************************************************************************
Set ERRORLEVEL=
if '%Role%' == 'server' (
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:connection -PrePostRecvs:3
)
if '%Role%' == 'client' (
   REM delay the client
   ping localhost -n 5 > nul
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:connection -PrePostRecvs:3
)

IF ERRORLEVEL 1 (
  echo TEST FAILED: this test is expected to succeed : %ERRORLEVEL%
  PAUSE
) else (
  echo PASSED
)


REM **********************************************************************************************
REM Test : verify:data
REM **********************************************************************************************
Set ERRORLEVEL=
if '%Role%' == 'server' (
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:data
)
if '%Role%' == 'client' (
   REM delay the client
   ping localhost -n 5 > nul
   cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:data
)

IF ERRORLEVEL 1 (
  echo TEST FAILED: this test is expected to succeed : %ERRORLEVEL%
  PAUSE
) else (
  echo PASSED
)



REM **********************************************************************************************
REM Test : sender not verifying, but receiver -verify:data, slight buffer count delta
REM **********************************************************************************************
Set ERRORLEVEL=
if '%Role%' == 'server' (
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:connection
)
if '%Role%' == 'client' (
   REM delay the client
   ping localhost -n 5 > nul
   cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:data
)

if /i '%1' == 'pull' (
  echo NOTE ** The client should have failed all calls with ErrorDataDidNotMatchBitPattern           **
  echo      ** This is expected, as the server was not keeping track of the buffer offset            **
  echo      ** and was sending with a different buffer size this the client eventually received data **
  echo      ** that did not continue to circle around the expected circular buffer of bytes          **
  
  IF '%ERRORLEVEL%' NEQ '%CONNECTIONS%' (
    echo TEST FAILED: this test is expected to fail : %ERRORLEVEL%
    PAUSE
  ) else (
    echo PASSED
  )
) else if /i '%1' == 'duplex' (
  echo NOTE ** The client should have failed all calls with ErrorDataDidNotMatchBitPattern           **
  echo      ** This is expected, as the server was not keeping track of the buffer offset            **
  echo      ** and was sending with a different buffer size this the client eventually received data **
  echo      ** that did not continue to circle around the expected circular buffer of bytes          **
  
  IF '%ERRORLEVEL%' NEQ '%CONNECTIONS%' (
    echo TEST FAILED: this test is expected to fail : %ERRORLEVEL%
    PAUSE
  ) else (
    echo PASSED
  )
) else (
  IF ERRORLEVEL 1 (
    echo TEST FAILED: this test is expected to succeed : %ERRORLEVEL%
    PAUSE
  ) else (
    echo PASSED
  )
)

REM **********************************************************************************************
REM Test : receiver not verifying, but sender -verify:data, slight buffer count delta
REM **********************************************************************************************
Set ERRORLEVEL=
if '%Role%' == 'server' (
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:data
)
if '%Role%' == 'client' (
   REM delay the client
   ping localhost -n 5 > nul
   cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:connection
)

if /i '%1' == 'push' (
  echo NOTE ** The server should have failed all calls with ErrorDataDidNotMatchBitPattern           **
  echo      ** This is expected, as the client was not keeping track of the buffer offset            **
  echo      ** and was sending with a different buffer size this the server eventually received data **
  echo      ** that did not continue to circle around the expected circular buffer of bytes          **
  
  IF '%ERRORLEVEL%' NEQ '%CONNECTIONS%' (
    echo TEST FAILED: this test is expected to fail : %ERRORLEVEL%
    PAUSE
  ) else (
    echo PASSED
  )
) else if /i '%1' == 'pushpull' (
  echo NOTE ** The server should have failed all calls with ErrorDataDidNotMatchBitPattern           **
  echo      ** This is expected, as the client was not keeping track of the buffer offset            **
  echo      ** and was sending with a different buffer size this the server eventually received data **
  echo      ** that did not continue to circle around the expected circular buffer of bytes          **
  
  IF '%ERRORLEVEL%' NEQ '%CONNECTIONS%' (
    echo TEST FAILED: this test is expected to fail : %ERRORLEVEL%
    PAUSE
  ) else (
    echo PASSED
  )
) else if /i '%1' == 'duplex' (
  echo NOTE ** The server should have failed all calls with ErrorDataDidNotMatchBitPattern           **
  echo      ** This is expected, as the client was not keeping track of the buffer offset            **
  echo      ** and was sending with a different buffer size this the server eventually received data **
  echo      ** that did not continue to circle around the expected circular buffer of bytes          **
  
  IF '%ERRORLEVEL%' NEQ '%CONNECTIONS%' (
    echo TEST FAILED: this test is expected to fail : %ERRORLEVEL%
    PAUSE
  ) else (
    echo PASSED
  )
) else (
  IF ERRORLEVEL 1 (
    echo TEST FAILED: this test is expected to succeed : %ERRORLEVEL%
    PAUSE
  ) else (
    echo PASSED
  )
)

:exit
