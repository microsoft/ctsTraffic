# Copilot Instructions for ctsTraffic

## Build

This is a C++ Visual Studio solution (`ctsTraffic.sln`) targeting Windows 10+ using MSBuild and the v145 platform toolset. NuGet packages must be restored before building.

```powershell
# Restore NuGet packages
nuget restore ctsTraffic.sln

# Build entire solution (Debug x64)
msbuild /p:Configuration=Debug /p:Platform=x64 ctsTraffic.sln

# Build a single project
msbuild /p:Configuration=Debug /p:Platform=x64 ctsTraffic\ctsTraffic.vcxproj
```

CI uses the `reusable-build.yml` workflow, building Debug and Release for x64 on `windows-2022`.

## Tests

Unit tests use **Microsoft Visual Studio CppUnitTest** (MSTest native). Each test project is under `MSTest/` and produces a separate DLL.

```powershell
# Run all tests
vstest.console.exe x64\Debug\ctsIOPatternStateUnitTest.dll x64\Debug\ctsSocketUnitTest.dll ...

# Run a single test project
vstest.console.exe x64\Debug\ctsIOPatternStateUnitTest.dll

# Run a single test by name
vstest.console.exe x64\Debug\ctsIOPatternStateUnitTest.dll /Tests:TestName
```

## Architecture

ctsTraffic is a client/server network performance measurement tool for Windows, built on Winsock and IO Completion Ports (IOCP).

### Key components

- **`ctsTraffic/`** — Main application. Entry point is `ctsTraffic.cpp` (`wmain`). Initializes Winsock, parses config, and starts the socket broker.
- **`ctl/`** — Reusable utility library (prefix `ct`). Provides socket address wrappers (`ctSockaddr`), IOCP thread pool (`ctThreadIocp`), timers, WMI helpers, ETW readers, and math utilities. These are header-only (`.hpp`).
- **`ctsPerf/`** — Separate tool for collecting TCP Extended Statistics (ESTATS) performance data.
- **`MSTest/`** — Native C++ unit tests using Microsoft CppUnitTest framework.
- **`TestScripts/`** — CMD-based acceptance and integration test scripts.

### Connection lifecycle

`ctsSocketBroker` manages a pool of `ctsSocketState` objects. Each `ctsSocketState` drives a `ctsSocket` through states: Creating → Connecting → InitiatingIo → Closing. The IO layer uses a pluggable pattern system:

- `ctsIoPattern` (base class) → `ctsIoPatternT<Stats, ProtocolPolicy, RateLimitPolicy>` (CRTP template)
- `ctsIoPatternState` — State machine tracking IO progress (connection ID exchange, data transfer, completion handshake)
- Policy classes in `*Policy.hpp` files control protocol behavior, rate limiting, and buffer management

### IO models

Multiple IO function implementations are available as pluggable strategies:
- `ctsReadWriteIocp` — ReadFile/WriteFile with IOCP (default, most scalable)
- `ctsSendRecvIocp` — WSASend/WSARecv with IOCP
- `ctsRioIocp` — Registered I/O (RIO) with IOCP
- `ctsMediaStreamClient/Server` — UDP media streaming

### Configuration

`ctsConfig` is a singleton namespace providing global settings parsed from command-line arguments. Many classes reference `ctsConfig` for runtime settings. Forward declarations are used extensively to avoid circular header dependencies.

## Code Quality Rules

See [instructions/cpp-code-quality.instructions.md](instructions/cpp-code-quality.instructions.md) for enforced C++ coding rules, including:
- File-scoped globals and helpers must be `static`
- Use `%ls` not `%ws` for wide string printf specifiers
- Prefer `std::println` / `std::format` over `printf` for new code
- Enforce const-correctness on input parameters
- Prefer range-based for loops over iterator loops
- Use descriptive loop variable names

## Conventions

### Naming

- **`cts` prefix** — ctsTraffic-specific classes/files (e.g., `ctsSocket`, `ctsIOPattern`)
- **`ct` prefix** — Reusable utility library classes in `ctl/` (e.g., `ctSockaddr`, `ctThreadIocp`)
- **Member variables** — `m_` prefix (e.g., `m_socket`, `m_lastError`)
- **Constants** — `c_` prefix (e.g., `c_statusIoRunning`, `c_completionMessageSize`)
- **Enums** — Scoped enums (`enum class`) with `PascalCase` values and explicit underlying type

### Header organization

Headers follow a strict include order, marked with comments:
```cpp
// cpp headers
#include <memory>
// os headers
#include <Windows.h>
#include <WinSock2.h>
// ctl headers
#include <ctSockaddr.hpp>
// project headers
#include "ctsConfig.h"
// wil headers always included last
#include <wil/resource.h>
```

### Dependencies

- **WIL (Windows Implementation Libraries)** — Used extensively for RAII wrappers (`wil::unique_socket`, `wil::critical_section`, `wil::unique_event_nothrow`, etc.) and error handling (`THROW_WIN32_MSG`). Included as a git submodule.
- **SAL annotations** — Used for buffer safety (`_Field_size_full_`, `_Guarded_by_`, `_Requires_lock_held_`)
- Circular header dependencies are managed via forward declarations with explicit comments explaining why

### File extensions

- `.h` / `.cpp` — Standard headers and implementation files for classes
- `.hpp` — Header-only implementations (policy classes, utilities, templates)

### Thread safety

Critical sections use `wil::critical_section` with a shared spinlock count (`ctsConfig::ctsConfigSettings::c_CriticalSectionSpinlock`).
Lock guards use `wil::cs_leave_scope_exit`.
Thread pool work items use `wil::unique_threadpool_work` and `wil::unique_threadpool_timer`.
