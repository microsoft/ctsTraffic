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
set NORMAL_TRANSFER=1000000
set SMALLER_TRANSFER=999999
set VERY_SMALL_TRANSFER=9999


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
set ServerOptions= -listen:* -buffer:101 -ServerExitLimit:%CONNECTIONS%
set ClientOptions= -target:localhost -buffer:100 -connections:%CONNECTIONS% -iterations:1

REM **********************************************************************************************
REM Test : verify:connection and a single pended send
REM **********************************************************************************************
Set ERRORLEVEL=
if '%Role%' == 'server' (
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:connection -PrePostSends:1 -transfer:%NORMAL_TRANSFER%
)
if '%Role%' == 'client' (
   REM delay the client
   ping localhost -n 5 > nul
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:connection -PrePostSends:1 -transfer:%NORMAL_TRANSFER%
)

IF ERRORLEVEL 1 (
  echo TEST FAILED: this test is expected to succeed : %ERRORLEVEL%
  PAUSE
) else (
  echo PASSED
)


REM **********************************************************************************************
REM Test : verify:data with ISB controlling sends
REM **********************************************************************************************
Set ERRORLEVEL=
if '%Role%' == 'server' (
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:data -transfer:%NORMAL_TRANSFER%
)
if '%Role%' == 'client' (
   REM delay the client
   ping localhost -n 5 > nul
   cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:data -transfer:%NORMAL_TRANSFER%
)

IF ERRORLEVEL 1 (
  echo TEST FAILED: this test is expected to succeed : %ERRORLEVEL%
  PAUSE
) else (
  echo PASSED
)


REM **********************************************************************************************
REM Test : verify:connection with multiple IO requests
REM **********************************************************************************************
Set ERRORLEVEL=
if '%Role%' == 'server' (
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:connection -PrePostRecvs:3 -transfer:%NORMAL_TRANSFER%
)
if '%Role%' == 'client' (
   REM delay the client
   ping localhost -n 5 > nul
  cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:connection -PrePostRecvs:3 -transfer:%NORMAL_TRANSFER%
)

IF ERRORLEVEL 1 (
  echo TEST FAILED: this test is expected to succeed : %ERRORLEVEL%
  PAUSE
) else (
  echo PASSED
)


REM **********************************************************************************************
REM Test : sender sending more bytes than receiver is expecting, slight buffer count delta
REM **********************************************************************************************
Set ERRORLEVEL=
Set EXPECTED_ERROR=
if '%Role%' == 'server' (

  if /i '%1' == 'push' (
    Set EXPECTED_ERROR=1
    cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:data -transfer:%VERY_SMALL_TRANSFER%

  ) else if /i '%1' == 'pull' (
    Set EXPECTED_ERROR=1
    cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:data -transfer:%NORMAL_TRANSFER%

  ) else if /i '%1' == 'pushpull' (
    Set EXPECTED_ERROR=1
    cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:data -transfer:%VERY_SMALL_TRANSFER%

  ) else if /i '%1' == 'duplex' (
    Set EXPECTED_ERROR=1
    cdb -gG -snul -sins -failinc  ctsTraffic.exe %ServerOptions% -pattern:%1 -io:%2 -verify:data -transfer:%VERY_SMALL_TRANSFER%
  )
)

if '%Role%' == 'client' (
  REM delay the client
  ping localhost -n 5 > nul

  if /i '%1' == 'push' (
    Set EXPECTED_ERROR=1
    cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:data -transfer:%NORMAL_TRANSFER%

  ) else if /i '%1' == 'pull' (
    Set EXPECTED_ERROR=1
    cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:data -transfer:%VERY_SMALL_TRANSFER%

  ) else if /i '%1' == 'pushpull' (
    Set EXPECTED_ERROR=1
    cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:data -transfer:%NORMAL_TRANSFER%

  ) else if /i '%1' == 'duplex' (
    Set EXPECTED_ERROR=1
    cdb -gG -snul -sins -failinc  ctsTraffic.exe %ClientOptions% -pattern:%1 -io:%2 -verify:data -transfer:%NORMAL_TRANSFER%
  )
)

if '%EXPECTED_ERROR%' == '0' (
  REM Expect this teset to succeed
  IF '%ERRORLEVEL%' NEQ '0' (
    echo TEST FAILED: this test is expected to PASS : %ERRORLEVEL%
    PAUSE
  ) else (
    echo PASSED -- all instances transferred successfully
  )
) else (
  IF '%ERRORLEVEL%' NEQ '%CONNECTIONS%' (
    echo TEST FAILED: this test is expected to FAIL : %ERRORLEVEL%
    PAUSE
  ) else (
    echo PASSED -- all instances failed to transfer
  )
)

:exit
