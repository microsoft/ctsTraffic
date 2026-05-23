# C++ Code Quality Rules — windows-networking-tools

Apply these rules when modifying C++ code in this repository.

---

## 1. Internal Linkage for File-Scoped Globals and Helpers

All file-scoped global variables and helper functions that are not used outside their translation unit **must** be declared `static`.

- Global flags, containers, and file-scoped objects → `static`
- Free functions that are only called within the same `.cpp` file → `static`

```cpp
// WRONG
bool g_my_flag = false;
void HelperFunction() noexcept { ... }

// RIGHT
static bool g_my_flag = false;
static void HelperFunction() noexcept { ... }
```

---

## 2. Use `%ls` Instead of `%ws` for Wide String Printf Format Specifiers

The `%ws` specifier is a Microsoft extension. Prefer the C standard `%ls` specifier for portable wide-string formatting in `std::printf`.

```cpp
// WRONG
std::printf("Processing %ws\n", filepath.c_str());

// RIGHT
std::printf("Replacing %ls\n", filepath.c_str());
```

---

## 3. Use `std::println` / `std::format` Instead of printf Where Possible

When C++23 `<print>` is available (this project includes `<print>`), prefer `std::println` and `std::format` over `std::printf` for new or modified output — especially for error messages that combine multiple format arguments.

```cpp
// WRONG (for new/modified code)
std::printf("ShellExecuteEx failed - gle 0x%x ShellError 0x%p\n", gle, hInst);

// RIGHT
println("ShellExecuteEx failed - gle {} ShellError 0x{:p}", gle, hInst);
```

Use the correct format specifier: `%lu` or `{}` for `DWORD`/unsigned values — never `%d` for unsigned types.

---

## 4. Const-Correctness on Input Parameters

Function parameters and references that are not modified **must** be declared `const`.

```cpp
// WRONG
static BomType Utf16Bom(std::vector<uint8_t>& buffer) noexcept;

// RIGHT
static BomType Utf16Bom(const std::vector<uint8_t>& buffer) noexcept;
```

---

## 5. Prefer Range-Based For Loops Over Iterator Loops

When iterating a container sequentially and the iterator itself is not needed (no `std::next`, no erase), use a range-based `for` loop.

```cpp
// WRONG
for (auto iter = buffer.cbegin(); iter != buffer.cend(); ++iter)
{
    const auto current_character = *iter;
    ...
}

// RIGHT
for (unsigned char current_character : buffer)
{
    ...
}
```

---

## 6. Single Point of Return

Functions should have a single `return` statement at the end rather than multiple early returns from the middle of the function body.
Assign to a result variable and use control flow (`else`, `break`) to reach the single return point.
This allows the compiler to optimize the call to the function using RVO (return value optimization).

```cpp
// WRONG
wsIOResult ctsWSARecvFrom(...) noexcept
{
    wsIOResult returnResult;
    try
    {
        ...
    }
    catch (...)
    {
        const auto error = ctsConfig::PrintThrownException();
        return wsIOResult(error); // early return
    }

    return returnResult;
}

// RIGHT
wsIOResult ctsWSARecvFrom(...) noexcept
{
    wsIOResult returnResult;
    try
    {
        ...
    }
    catch (...)
    {
        const auto error = ctsConfig::PrintThrownException();
        returnResult = wsIOResult(error); // assign, don't return
    }

    return returnResult; // single return
}
```

---

## 7. Prefer Designated Initializers for Struct Initialization

When initializing C-style structs, use C++20 designated initializers (`{ .member = value }`) instead of declaring the variable and assigning members separately.
Only apply when setting all members of the structure. Default to initializing all fields, but if only a subset of fields are being initialized and if the structure was initially zero'd, do not make changes.

```cpp
// WRONG
WSABUF wsabuffer;
wsabuffer.buf = task.m_buffer + task.m_bufferOffset;
wsabuffer.len = task.m_bufferLength;

linger lingerOption{};
lingerOption.l_onoff = 1;
lingerOption.l_linger = 0;

// RIGHT
WSABUF wsa_buffer{
    .len = task.m_bufferLength,
    .buf = task.m_buffer + task.m_bufferOffset
};

linger lingerOption{ .l_onoff = 1, .l_linger = 0 };
```

---
