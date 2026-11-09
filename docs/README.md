# cdl 详细文档

> 版本：1.0.0  
> 适用平台：Windows、Linux、Android  
> 构建系统：CMake  
> 实现语言：C99

`cdl` 是一个小型的动态库加载包装库。它把三套不同的系统动态加载 API 收敛为一组 C 接口：

| 平台 | 底层 API |
|---|---|
| Linux | `dlopen` / `dlsym` / `dlclose` / `dlerror` |
| Android | bionic 的 `dlopen` 家族 |
| Windows | `LoadLibraryW` / `GetProcAddress` / `FreeLibrary` / `FormatMessageW` |

它的目标不是重新实现动态链接器，而是在 C 能表达的范围内提供一致、可诊断、可移植的加载接口。

---

## 目录

1. [项目定位](#1-项目定位)
2. [快速开始](#2-快速开始)
3. [构建与安装](#3-构建与安装)
4. [集成到项目](#4-集成到项目)
5. [公共 API 参考](#5-公共-api-参考)
6. [使用示例](#6-使用示例)
7. [平台行为详解](#7-平台行为详解)
8. [错误处理](#8-错误处理)
9. [实现细节](#9-实现细节)
10. [测试与验证](#10-测试与验证)
11. [边界与未实现能力](#11-边界与未实现能力)
12. [故障排查](#12-故障排查)
13. [版本与 ABI 约定](#13-版本与-abi-约定)
14. [License](#14-license)
15. [附录 A：头文件速查](#附录-a头文件速查)
16. [附录 B：平台选择速查](#附录-b平台选择速查)
17. [附录 C：最小错误处理模板](#附录-c最小错误处理模板)

---

## 1. 项目定位

### 1.1 设计目标

- **只依赖 C**：库本体是 C99，不依赖 C++ 运行时、STL 或异常机制。
- **一套接口**：同一份调用代码可以在 Linux、Android、Windows 上编译。
- **显式错误**：加载失败时返回 `NULL`，通过线程局部错误状态读取错误码和可读消息。
- **不做魔法**：不模拟其他平台的动态链接器搜索规则，不隐藏平台差异。
- **可静态链接、也可动态链接**：由 CMake 选项控制。
- **可安装、可被消费**：提供 `cdl::cdl` CMake 目标与 `cdl.pc`。

### 1.2 非目标

`cdl` 不试图成为“完整的 libdl 替代品”。以下内容不属于目标：

- 符号版本查询（`dlvsym`）。
- 地址反查模块名（`dladdr`）。
- 遍历已加载模块（`dl_iterate_phdr`）。
- 透明包装 `RTLD_DEFAULT` / `RTLD_NEXT` 的调用点语义。
- Windows 上的 PE 重定向、按序号导入、清单文件等高级加载特性。
- 跨平台统一动态库搜索路径。

这些能力的缺失是有意的，原因见[边界与未实现能力](#11-边界与未实现能力)。

### 1.3 术语

| 术语 | 含义 |
|---|---|
| 模块 / module | 一个共享库：ELF `.so`、PE `.dll` |
| 句柄 / handle | `cdl_open` 成功后返回的不透明指针 |
| 符号 / symbol | 模块导出的函数或数据对象 |
| 主程序句柄 | `cdl_open(NULL, ...)` 返回的当前可执行文件句柄 |
| TLS | Thread-Local Storage，线程局部存储 |

---

## 2. 快速开始

### 2.1 最小程序

```c
#include <cdl/cdl.h>
#include <stdio.h>
#include <string.h>

typedef int (*answer_fn)(void);

int main(void)
{
    cdl_handle_t handle;
    void *symbol;
    answer_fn answer = NULL;

    handle = cdl_open("libexample.so", CDL_NOW | CDL_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "load failed: %s\n", cdl_error());
        return 1;
    }

    symbol = cdl_sym(handle, "answer");
    if (symbol == NULL) {
        fprintf(stderr, "lookup failed: %s\n", cdl_error());
        (void)cdl_close(handle);
        return 1;
    }

    /*
     * C 标准不保证 void* 与函数指针可以自由转换。
     * 这里使用 memcpy 把地址复制到函数指针变量。
     */
    memcpy(&answer, &symbol, sizeof(answer));

    printf("answer = %d\n", answer());
    (void)cdl_close(handle);
    return 0;
}
```

在 Windows 上，把库名换成 `.dll`：

```c
handle = cdl_open("example.dll", CDL_NOW | CDL_LOCAL);
```

### 2.2 最短路径

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

安装：

```sh
cmake --install build --prefix /your/prefix
```

然后在其他 CMake 项目中：

```cmake
find_package(cdl 1.0 CONFIG REQUIRED)
target_link_libraries(app PRIVATE cdl::cdl)
```

---

## 3. 构建与安装

### 3.1 依赖

| 依赖 | 最低要求 | 说明 |
|---|---|---|
| C 编译器 | C99 | GCC、Clang、MSVC 均可；MSVC 建议 2015+ |
| CMake | 3.16 | 需要 `GNUInstallDirs`、`CMakePackageConfigHelpers` |
| Ninja（可选） | 任意 | 也可以用 Makefile 或 Visual Studio 生成器 |
| Android NDK | 任意较新版本 | 已在 r26d 上验证 |

Linux 和 Android 需要链接系统 `dl` 库；CMake 通过 `CMAKE_DL_LIBS` 自动处理。glibc 2.34+ 已把动态加载符号并入 libc，但保留 `-ldl` 仍然兼容。

### 3.2 Linux

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

构建产物（共享库）：

```text
build/libcdl.so -> libcdl.so.1 -> libcdl.so.1.0.0
build/tests/cdl_tests
build/tests/cdl_test_module.so
build/examples/cdl_example
```

静态库：

```sh
cmake -S . -B build-static -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCDL_BUILD_SHARED=OFF
cmake --build build-static
ctest --test-dir build-static --output-on-failure
```

产物为 `build-static/libcdl.a`。

安装：

```sh
cmake --install build --prefix /usr/local
```

### 3.3 Windows（MSVC）

在 “x64 Native Tools Command Prompt for VS” 中：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix C:\path\to\install
```

共享库产物通常为：

```text
build\Release\cdl.dll
build\Release\cdl.lib
```

静态库产物为 `cdl.lib`（与导入库同名，但由 `CDL_BUILD_SHARED=OFF` 决定内容）。

### 3.4 Windows（MinGW-w64 交叉编译）

在 Linux 上交叉编译：

```sh
cmake -S . -B build-win -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```

MinGW 共享库通常为：

```text
build-win/libcdl.dll
build-win/libcdl.dll.a
```

MinGW 默认动态链接 `libgcc_s_*.dll`。64 位 SEH 版本是 `libgcc_s_seh-1.dll`。部署时：

- 把对应的 `libgcc_s_*.dll` 和程序放在一起；或
- 链接时加 `-static-libgcc`。

### 3.5 Android（NDK）

```sh
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCDL_BUILD_TESTS=OFF \
  -DCDL_BUILD_EXAMPLES=OFF
cmake --build build-android
```

产物为 `libcdl.so`。静态库：

```sh
-DCDL_BUILD_SHARED=OFF
```

产物为 `libcdl.a`。

在 Gradle 项目中，可以把 cdl 作为 CMake 子项目，或者预编译后把 `libcdl.so` 放入对应 ABI 的 `jniLibs` 目录。`cdl` 本身不依赖 Android Java 层。

### 3.6 CMake 选项

| 选项 | 默认值 | 说明 |
|---|---|---|
| `CDL_BUILD_SHARED` | `ON` | `ON`：共享库；`OFF`：静态库 |
| `CDL_BUILD_TESTS` | 顶层构建时 `ON` | 构建测试目标；作为子项目时默认 `OFF` |
| `CDL_BUILD_EXAMPLES` | 顶层构建时 `ON` | 构建示例；作为子项目时默认 `OFF` |
| `CDL_INSTALL` | 顶层构建时 `ON` | 生成 install、CMake package、pkg-config 规则 |

注意：

- cdl 使用自己的 `CDL_BUILD_SHARED`，不读取全局 `BUILD_SHARED_LIBS`。
- 作为子项目时，如果希望安装 cdl，需要显式传 `-DCDL_INSTALL=ON`。
- 测试和示例的默认值由“是否为顶层项目”决定，避免污染父项目。

### 3.7 安装布局

Linux / Android 典型布局：

```text
<prefix>/include/cdl/cdl.h
<prefix>/lib/libcdl.so.1.0.0
<prefix>/lib/libcdl.so.1
<prefix>/lib/libcdl.so
<prefix>/lib/cmake/cdl/cdlConfig.cmake
<prefix>/lib/cmake/cdl/cdlConfigVersion.cmake
<prefix>/lib/cmake/cdl/cdlTargets.cmake
<prefix>/lib/pkgconfig/cdl.pc
<prefix>/share/doc/cdl/README.md
<prefix>/share/doc/cdl/docs/README.md
<prefix>/share/doc/cdl/LICENSE
```

Windows 共享库会把 DLL 安装到 `bin`，导入库安装到 `lib`：

```text
<prefix>/bin/cdl.dll          （MSVC）
<prefix>/bin/libcdl.dll       （MinGW）
<prefix>/lib/cdl.lib          （MSVC 导入库）
<prefix>/lib/libcdl.dll.a     （MinGW 导入库）
```

具体文档文件名以安装规则为准；核心安装内容是头文件、库文件、CMake package 和 pkg-config 文件。

---

## 4. 集成到项目

### 4.1 作为 CMake 子项目

目录结构示例：

```text
my_project/
├── CMakeLists.txt
├── src/
└── third_party/
    └── cdl/
```

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_project C)

add_subdirectory(third_party/cdl)

add_executable(my_app src/main.c)
target_link_libraries(my_app PRIVATE cdl::cdl)
```

作为子项目时，`CDL_BUILD_TESTS`、`CDL_BUILD_EXAMPLES`、`CDL_INSTALL` 默认关闭。`cdl::cdl` 目标始终可用。

### 4.2 使用安装后的 CMake 包

```cmake
find_package(cdl 1.0 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE cdl::cdl)
```

如果 cdl 安装在非标准前缀：

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/your/prefix
```

`find_package` 会找到：

```text
<prefix>/lib/cmake/cdl/cdlConfig.cmake
```

`cdl::cdl` 是一个 imported target，会自动带上：

- 头文件目录；
- 库文件；
- 静态链接时所需的 `dl`（Linux/Android）；
- Windows 静态库的 `CDL_STATIC` 编译定义。

### 4.3 pkg-config

```sh
cc app.c $(pkg-config --cflags --libs cdl) -o app
```

静态链接：

```sh
cc app.c $(pkg-config --cflags --libs --static cdl) -o app
```

如果安装前缀不在默认搜索路径：

```sh
export PKG_CONFIG_PATH=/your/prefix/lib/pkgconfig:$PKG_CONFIG_PATH
```

### 4.4 手工编译

Linux 共享库：

```sh
cc app.c -I/your/prefix/include -L/your/prefix/lib -lcdl -ldl -o app
```

Linux 静态库：

```sh
cc app.c -I/your/prefix/include \
  /your/prefix/lib/libcdl.a -ldl -o app
```

Windows + MSVC 共享库：

```bat
cl app.c /I C:\cdl\include cdl.lib
```

确保 `cdl.dll` 在 `PATH` 或可执行文件同目录。静态库：

```bat
cl /DCDL_STATIC app.c /I C:\cdl\include cdl.lib
```

MinGW 静态库：

```sh
x86_64-w64-mingw32-gcc -DCDL_STATIC app.c \
  -I/your/prefix/include /your/prefix/lib/libcdl.a -o app.exe
```

手工构建 cdl 本身时：

- 构建 Windows DLL：定义 `CDL_BUILDING`；
- 使用 Windows 静态库：定义 `CDL_STATIC`；
- 使用 Windows 导入库：不要定义 `CDL_STATIC`、`CDL_BUILDING`，头文件默认使用 `dllimport`。

### 4.5 C++ 调用方

头文件包含：

```c
#ifdef __cplusplus
extern "C" {
#endif
```

因此可以直接被 C++ 包含。函数指针转换仍建议使用 `memcpy`：

```cpp
#include <cdl/cdl.h>
#include <cstring>

using create_fn = void *(*)(void);

void *raw = cdl_sym(handle, "create_object");
create_fn create = nullptr;
std::memcpy(&create, &raw, sizeof(create));
```

---

## 5. 公共 API 参考

公共头文件：

```c
#include <cdl/cdl.h>
```

### 5.1 平台宏

头文件会根据平台定义其中一个宏：

| 宏 | 平台 |
|---|---|
| `CDL_PLATFORM_WINDOWS` | Windows |
| `CDL_PLATFORM_LINUX` | Linux |
| `CDL_PLATFORM_ANDROID` | Android |

在不支持的平台上包含头文件会触发编译错误：

```c
#error "cdl supports Windows, Linux and Android only"
```

### 5.2 版本宏

```c
#define CDL_VERSION_MAJOR 1
#define CDL_VERSION_MINOR 0
#define CDL_VERSION_PATCH 0
#define CDL_VERSION_STRING "1.0.0"
```

### 5.3 `cdl_handle_t`

```c
typedef void *cdl_handle_t;
```

语义：

- 由 `cdl_open` 返回；
- `NULL` 永远不是合法句柄；
- POSIX 上底层就是 `dlopen` 返回的 `void *`；
- Windows 上底层是 `HMODULE`；
- 每个成功句柄必须且只能调用一次 `cdl_close`。

### 5.4 打开标志 `cdl_open_flag_t`

```c
typedef enum cdl_open_flag {
    CDL_LAZY     = 1u << 0,
    CDL_NOW      = 1u << 1,
    CDL_GLOBAL   = 1u << 2,
    CDL_LOCAL    = 1u << 3,
    CDL_DEEPBIND = 1u << 4,
    CDL_NODELETE = 1u << 5,
    CDL_NOLOAD   = 1u << 6
} cdl_open_flag_t;
```

| 标志 | 值 | Linux | Android | Windows |
|---|---:|---|---|---|
| `CDL_LAZY` | `0x01` | `RTLD_LAZY` | `RTLD_LAZY` | 接受，无效果 |
| `CDL_NOW` | `0x02` | `RTLD_NOW` | `RTLD_NOW` | 接受，无效果 |
| `CDL_GLOBAL` | `0x04` | `RTLD_GLOBAL` | `RTLD_GLOBAL` | 接受，无效果 |
| `CDL_LOCAL` | `0x08` | `RTLD_LOCAL` | `RTLD_LOCAL` | 接受，无效果 |
| `CDL_DEEPBIND` | `0x10` | `RTLD_DEEPBIND`（glibc） | 不支持 | 不支持 |
| `CDL_NODELETE` | `0x20` | `RTLD_NODELETE` | bionic 提供时 | 不支持 |
| `CDL_NOLOAD` | `0x40` | `RTLD_NOLOAD` | bionic 提供时 | `GetModuleHandleExW` |

互斥关系：

- `CDL_LAZY` 与 `CDL_NOW` 同时出现 → `CDL_ERROR_INVALID_ARGUMENT`；
- `CDL_GLOBAL` 与 `CDL_LOCAL` 同时出现 → `CDL_ERROR_INVALID_ARGUMENT`；
- 两者都不给时，默认使用 `CDL_NOW | CDL_LOCAL`；
- 未知位会被拒绝。

> 为什么 Windows 上 `CDL_LAZY` / `CDL_NOW` / `CDL_GLOBAL` / `CDL_LOCAL` 不报错？
>
> 这些标志在 Windows 上没有等价语义：PE 加载器没有 POSIX 那种“延迟绑定”开关，也没有“局部/全局导出”之分。为了让同一份跨平台调用代码不必到处写 `#ifdef`，cdl 接受它们，但并不改变加载行为。真正无法等价实现的 `CDL_DEEPBIND` 和 `CDL_NODELETE` 则会明确返回错误。

### 5.5 `cdl_open`

```c
cdl_handle_t cdl_open(const char *path, unsigned int flags);
```

#### 参数

| 参数 | 说明 |
|---|---|
| `path` | 模块路径。Windows 必须是 UTF-8；Linux/Android 是原生字节串。允许 `NULL` |
| `flags` | `cdl_open_flag_t` 的按位或；`0` 表示 `CDL_NOW \| CDL_LOCAL` |

#### 返回值

| 情况 | 返回值 |
|---|---|
| 成功 | 非 `NULL` 句柄 |
| 失败 | `NULL`；调用 `cdl_error()` 获取文本，调用 `cdl_last_error()` 获取错误码 |

#### `path == NULL`

打开主程序：

- Linux/Android：`dlopen(NULL, flags)`；
- Windows：通过 `GetModuleHandleExW(0, NULL, ...)` 取得可执行模块句柄，并增加引用计数；`cdl_close` 会通过 `FreeLibrary` 平衡。

Windows 上只能通过 `GetProcAddress` 查到该 EXE **导出**的符号。普通 EXE 通常不导出内部函数。

#### 错误

| 错误码 | 触发条件 |
|---|---|
| `CDL_ERROR_INVALID_ARGUMENT` | flags 非法、Windows 路径不是合法 UTF-8、路径过长 |
| `CDL_ERROR_OUT_OF_MEMORY` | Windows 路径转换分配失败 |
| `CDL_ERROR_NOT_SUPPORTED` | 平台不支持 `CDL_DEEPBIND` / `CDL_NODELETE` |
| `CDL_ERROR_OPEN_FAILED` | `dlopen` / `LoadLibraryW` / `GetModuleHandleExW` 失败 |

#### 示例

```c
cdl_handle_t h = cdl_open("plugins/audio.so", CDL_NOW | CDL_LOCAL);
if (h == NULL) {
    fprintf(stderr, "open failed: %s (code=%d)\n",
            cdl_error(), (int)cdl_last_error());
}
```

Windows：

```c
cdl_handle_t h = cdl_open("plugins\\audio.dll", CDL_NOW | CDL_LOCAL);
```

### 5.6 `cdl_sym`

```c
void *cdl_sym(cdl_handle_t handle, const char *name);
```

#### 参数

| 参数 | 说明 |
|---|---|
| `handle` | 非 `NULL` 的有效句柄 |
| `name` | 非空符号名。POSIX 为字节串；Windows 为 `GetProcAddress` 的 ANSI 名称 |

#### 返回值

| 情况 | 返回值 |
|---|---|
| 找到符号 | 符号地址 |
| 符号值为 `NULL`（POSIX 数据符号） | `NULL`，且 `cdl_last_error() == CDL_ERROR_NONE` |
| 未找到/出错 | `NULL`，且 `cdl_last_error() == CDL_ERROR_SYMBOL_NOT_FOUND` |

#### 错误

| 错误码 | 触发条件 |
|---|---|
| `CDL_ERROR_INVALID_ARGUMENT` | `handle == NULL`，或 `name` 为 `NULL`/空串 |
| `CDL_ERROR_SYMBOL_NOT_FOUND` | `dlsym` 报错，或 `GetProcAddress` 失败 |

#### 关于函数指针

`cdl_sym` 返回 `void *`。C 标准保证对象指针和 `void *` 之间可以转换，但不保证函数指针与 `void *` 之间可以自由转换。

推荐做法：

```c
typedef int (*compute_fn)(int);

void *raw = cdl_sym(handle, "compute");
if (raw == NULL && cdl_last_error() != CDL_ERROR_NONE) {
    /* 错误 */
}

compute_fn compute = NULL;
memcpy(&compute, &raw, sizeof(compute));
```

在 cdl 支持的所有目标 ABI 上，函数指针和 `void *` 大小相同；测试中有静态断言检查这一点。不要直接把 `void *` 强转成函数指针并假设完全可移植。

#### 符号可见性

Windows 只查找导出表：

```bat
dumpbin /exports example.dll
```

Linux/Android 需要符号在动态符号表中：

```sh
nm -D --defined-only libexample.so
```

C++ 符号还需要考虑 `extern "C"` 和名称修饰（mangling）。如果插件由 C++ 编写，导出函数应声明为：

```cpp
extern "C" __attribute__((visibility("default"))) int compute(int x);
```

Windows 上则使用 `__declspec(dllexport)`。

### 5.7 `cdl_close`

```c
int cdl_close(cdl_handle_t handle);
```

释放 `cdl_open` 获得的引用：

- POSIX：`dlclose(handle)`；
- Windows：`FreeLibrary((HMODULE)handle)`。

返回值：

| 情况 | 返回值 |
|---|---|
| 成功 | `0` |
| 失败 | `-1` |

错误：

| 错误码 | 触发条件 |
|---|---|
| `CDL_ERROR_INVALID_ARGUMENT` | `handle == NULL` |
| `CDL_ERROR_CLOSE_FAILED` | `dlclose` 或 `FreeLibrary` 失败 |

约定：

- 每个成功 `cdl_open` 只调用一次 `cdl_close`；
- 关闭成功后不得继续使用该句柄；
- 不要在另一个线程同时 `cdl_sym` 和 `cdl_close` 同一个句柄，生命周期由调用方同步；
- 同一模块被打开多次时，底层引用计数由系统加载器维护。

### 5.8 `cdl_error`

```c
const char *cdl_error(void);
```

返回当前线程最近一次错误消息；没有错误时返回 `NULL`。

重要约定：

- 返回的指针由 cdl 内部持有，不要 `free`；
- 指针有效期到**同一线程**下一次 `cdl_open`、`cdl_sym`、`cdl_close` 或 `cdl_error_clear` 调用为止；查询函数本身不会使指针失效；
- 错误状态是线程局部的；
- 错误文本用于人类阅读，不要用它做程序逻辑判断；请使用 `cdl_last_error()` 的枚举值。

### 5.9 `cdl_last_error`

```c
cdl_error_t cdl_last_error(void);
```

返回当前线程的错误码。成功操作后为 `CDL_ERROR_NONE`。

### 5.10 `cdl_error_clear`

```c
void cdl_error_clear(void);
```

清除当前线程的错误状态。不会影响其他线程，也不会卸载任何模块。

### 5.11 `cdl_version`

```c
const char *cdl_version(void);
```

返回 `"1.0.0"` 形式的版本字符串。指针指向静态存储，不需要释放。

### 5.12 错误码 `cdl_error_t`

```c
typedef enum cdl_error_code {
    CDL_ERROR_NONE = 0,
    CDL_ERROR_INVALID_ARGUMENT,
    CDL_ERROR_OUT_OF_MEMORY,
    CDL_ERROR_NOT_SUPPORTED,
    CDL_ERROR_OPEN_FAILED,
    CDL_ERROR_SYMBOL_NOT_FOUND,
    CDL_ERROR_CLOSE_FAILED
} cdl_error_t;
```

| 枚举 | 含义 | 典型来源 |
|---|---|---|
| `CDL_ERROR_NONE` | 无错误 | 成功操作 |
| `CDL_ERROR_INVALID_ARGUMENT` | 参数非法 | 空句柄、空符号名、互斥 flags、非法 UTF-8 路径 |
| `CDL_ERROR_OUT_OF_MEMORY` | 内存不足 | Windows UTF-8 → UTF-16 转换分配失败 |
| `CDL_ERROR_NOT_SUPPORTED` | 当前平台不支持 | Windows 的 `CDL_DEEPBIND` / `CDL_NODELETE`；Android 的 `CDL_DEEPBIND` |
| `CDL_ERROR_OPEN_FAILED` | 模块加载失败 | `dlopen` / `LoadLibraryW` / `GetModuleHandleExW` |
| `CDL_ERROR_SYMBOL_NOT_FOUND` | 符号查找失败 | `dlsym` 报错 / `GetProcAddress` 失败 |
| `CDL_ERROR_CLOSE_FAILED` | 卸载失败 | `dlclose` / `FreeLibrary` |

---

## 6. 使用示例

### 6.1 加载并调用函数

```c
#include <cdl/cdl.h>
#include <stdio.h>
#include <string.h>

typedef int (*add_fn)(int, int);

static int load_add(const char *path)
{
    cdl_handle_t handle;
    void *symbol;
    add_fn add = NULL;

    handle = cdl_open(path, CDL_NOW | CDL_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "open: %s\n", cdl_error());
        return -1;
    }

    symbol = cdl_sym(handle, "add");
    if (symbol == NULL) {
        fprintf(stderr, "sym: %s\n", cdl_error());
        (void)cdl_close(handle);
        return -1;
    }

    memcpy(&add, &symbol, sizeof(add));
    printf("add(2, 3) = %d\n", add(2, 3));

    return cdl_close(handle) == 0 ? 0 : -1;
}

int main(void)
{
    return load_add("./libexample.so");
}
```

### 6.2 读取数据符号

```c
void *symbol = cdl_sym(handle, "plugin_version");
if (symbol != NULL) {
    int version;
    memcpy(&version, symbol, sizeof(version));
    printf("plugin version: %d\n", version);
} else if (cdl_last_error() != CDL_ERROR_NONE) {
    fprintf(stderr, "not found: %s\n", cdl_error());
} else {
    /* POSIX：符号存在，但值为 NULL */
}
```

### 6.3 只检查模块是否已加载

```c
cdl_handle_t handle = cdl_open("libaudio.so",
                               CDL_NOW | CDL_NOLOAD);
if (handle == NULL) {
    /* 未加载，或平台不支持 NOLOAD */
    printf("not loaded: %s\n", cdl_error());
} else {
    printf("already loaded\n");
    cdl_close(handle);
}
```

注意：`CDL_NOLOAD` 的底层语义由平台决定；glibc 上它返回的引用计数行为请参考 `dlopen(3)`。

### 6.4 可选插件模式

```c
static int load_optional(const char *path)
{
    cdl_handle_t handle = cdl_open(path, CDL_NOW | CDL_LOCAL);

    if (handle == NULL) {
        /* 插件缺失通常不是致命错误 */
        fprintf(stderr, "optional plugin skipped: %s\n", cdl_error());
        return 0;
    }

    /* ... 查找符号、调用初始化函数 ... */

    cdl_close(handle);
    return 0;
}
```

### 6.5 打开主程序

```c
cdl_handle_t self = cdl_open(NULL, CDL_NOW | CDL_LOCAL);
if (self == NULL) {
    fprintf(stderr, "self: %s\n", cdl_error());
    return 1;
}

/*
 * POSIX 上可以查找主程序及其依赖的全局符号；
 * Windows 上只能查找 EXE 导出表中的符号。
 */
void *symbol = cdl_sym(self, "main");
(void)symbol;

cdl_close(self);
```

### 6.6 统一错误报告

```c
static void report_error(const char *operation)
{
    const char *message = cdl_error();
    fprintf(stderr, "%s failed: %s (code %d)\n",
            operation,
            message != NULL ? message : "unknown error",
            (int)cdl_last_error());
}
```

### 6.7 插件生命周期建议

- 启动时加载、退出时卸载：最简单，适合大多数插件系统。
- 长期驻留：加载后不调用 `cdl_close`，让进程退出时由系统回收；但要明确所有权。
- 热重载：先卸载旧模块，再加载新模块；确保没有线程还在调用旧模块的代码或访问其数据。
- 不要在模块内部调用 `cdl_close` 卸载自己，行为依平台而定，容易崩溃。

---

## 7. 平台行为详解

### 7.1 能力矩阵

| 能力 | Linux | Android | Windows |
|---|---|---|---|
| 加载模块 | `dlopen` | bionic `dlopen` | `LoadLibraryW` |
| 查找符号 | `dlsym` | bionic `dlsym` | `GetProcAddress` |
| 卸载模块 | `dlclose` | bionic `dlclose` | `FreeLibrary` |
| 延迟绑定 | 支持 | 支持 | 无此概念 |
| 全局/局部导出 | 支持 | 支持 | 无此概念 |
| 深绑定 | glibc 支持 | 不支持 | 不支持 |
| 不卸载 | 支持 | 视 bionic 版本 | 不支持 |
| 只查询不加载 | 支持 | 视 bionic 版本 | 支持 |
| 路径编码 | 原生字节 | 原生字节 | UTF-8 → UTF-16 |
| 错误文本 | `dlerror()` | `dlerror()` | `FormatMessageW` → UTF-8 |
| 主程序句柄 | `dlopen(NULL)` | `dlopen(NULL)` | 可执行模块 |
| 函数符号 | 支持 | 支持 | 支持 |
| 数据符号 | 支持 | 支持 | 支持（若已导出） |

### 7.2 Linux

#### 底层调用

```c
void *handle = dlopen(path, flags);
void *symbol = dlsym(handle, name);
int result = dlclose(handle);
const char *error = dlerror();
```

#### 搜索规则

`dlopen` 的搜索行为取决于 `path` 和系统配置：

- `path` 含 `/`：按路径直接打开；
- 不含 `/`：按 `DT_RPATH`、`LD_LIBRARY_PATH`、`DT_RUNPATH`、`ld.so.cache`、`/lib`、`/usr/lib` 等规则查找；
- 具体顺序受 glibc 版本、`DF_1_NODEFLIB`、安全模式等影响。

cdl 不改变这些规则，也不会自行拼接搜索目录。

#### 标志映射

| cdl | glibc |
|---|---|
| `CDL_LAZY` | `RTLD_LAZY` |
| `CDL_NOW` | `RTLD_NOW` |
| `CDL_GLOBAL` | `RTLD_GLOBAL` |
| `CDL_LOCAL` | `RTLD_LOCAL` |
| `CDL_DEEPBIND` | `RTLD_DEEPBIND` |
| `CDL_NODELETE` | `RTLD_NODELETE` |
| `CDL_NOLOAD` | `RTLD_NOLOAD` |

`CDL_DEEPBIND` 是 glibc 扩展，会改变符号解析优先级；使用前请确认它确实是你需要的语义，并注意它可能带来的符号冲突和安全影响。

#### 链接

CMake 通过 `CMAKE_DL_LIBS` 传递 `dl`。手工编译时通常需要：

```sh
cc app.c -lcdl -ldl
```

### 7.3 Android

#### bionic 与 libdl

Android 使用 bionic libc 和 bionic linker。NDK 提供 `<dlfcn.h>` 和 `libdl.so`：

- 旧 API 级别：`dlopen` 等符号位于 `libdl.so`；
- Android 7.0 / API 24 起：动态加载实现逐步移入 libc，`libdl.so` 作为兼容 stub 存在。

CMake 在 Android 工具链下会把 `dl` 链接进 cdl。使用 cdl 的应用不需要额外写 `-ldl`（但写上通常也无害）。

#### flags

- `CDL_LAZY`、`CDL_NOW`、`CDL_GLOBAL`、`CDL_LOCAL`：可用；
- `CDL_NOLOAD`、`CDL_NODELETE`：NDK 头文件提供时可用，实际行为取决于设备 bionic linker；
- `CDL_DEEPBIND`：bionic 不提供，cdl 返回 `CDL_ERROR_NOT_SUPPORTED`。

#### 命名空间限制

Android 的 linker namespace 会限制应用能加载哪些系统库。应用通常可以：

- 加载自己 APK 中打包的 native 库；
- 加载公开 NDK 库；
- 在满足命名空间规则时加载系统库。

直接 `dlopen` 任意平台私有 `.so`（例如某些 `/system/lib*` 下的库）可能失败，即使文件存在。这是 Android 平台安全模型，不是 cdl 能绕过的。

#### 打包建议

- 把 cdl 和插件放进正确的 ABI 目录（`arm64-v8a`、`armeabi-v7a`、`x86_64` 等）。
- 优先用库名让 Android linker 在自己的命名空间中解析，例如 `"libplugin.so"`；不要假设 APK 内文件的绝对路径。
- 确保 cdl 与插件的 ABI、API level、STL 运行时一致。
- 如果插件依赖其他 `.so`，把这些依赖一起打包，并处理加载顺序。

#### 测试

Android 上测试可以编译，但 CMake 不会注册 CTest（因为测试必须在设备/模拟器上运行）。手动运行方式见[测试与验证](#10-测试与验证)。

### 7.4 Windows

#### 底层调用

```c
HMODULE module = LoadLibraryW(wide_path);
FARPROC proc = GetProcAddress(module, name);
BOOL ok = FreeLibrary(module);
```

#### 路径编码

`cdl_open` 在 Windows 上接收 **UTF-8** 路径，内部使用：

```c
MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...)
```

转换为 UTF-16 后调用 `LoadLibraryW`。因此：

- 路径可以包含中文、日文、emoji 等 Unicode 字符；
- 非法 UTF-8 会返回 `CDL_ERROR_INVALID_ARGUMENT`；
- 路径过长、转换分配失败会返回对应错误。

如果路径来自 `main()` 的 `argv`，注意 Windows CRT 通常按当前 ANSI 代码页提供 `argv`，并不保证是 UTF-8。需要：

- 使用 `wmain()` 获取宽字符参数后自行转 UTF-8；或
- 为程序启用 UTF-8 活动代码页（Windows 10 1903+）；或
- 从其他 UTF-8 来源获取路径。

#### `LoadLibraryW` 搜索规则

`cdl_open` 使用 `LoadLibraryW` 的默认搜索规则，受系统 Safe DLL Search Mode、`SetDllDirectory`、`AddDllDirectory`、应用目录、当前目录、`PATH` 等影响。cdl 不做额外路径拼接或重排。

如果需要严格控制 DLL 搜索路径，调用方应：

- 使用绝对路径；
- 使用 `SetDefaultDllDirectories` / `AddDllDirectory` / `LoadLibraryEx` 等 Windows 机制；
- 或把 DLL 放在应用目录并遵循系统搜索策略。

#### flags 语义

| cdl | Windows |
|---|---|
| `CDL_LAZY` | 接受，无效果（PE 加载器没有延迟绑定开关） |
| `CDL_NOW` | 接受，无效果 |
| `CDL_GLOBAL` | 接受，无效果（导出是进程级可见） |
| `CDL_LOCAL` | 接受，无效果 |
| `CDL_DEEPBIND` | 不支持，返回 `CDL_ERROR_NOT_SUPPORTED` |
| `CDL_NODELETE` | 不支持，返回 `CDL_ERROR_NOT_SUPPORTED` |
| `CDL_NOLOAD` | 通过 `GetModuleHandleExW` 查询已加载模块 |

#### `CDL_NOLOAD`

底层使用：

```c
GetModuleHandleExW(0, wide_path, &module);
```

不带 `GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT`，因此会增加引用计数；随后 `cdl_close` 调用 `FreeLibrary` 平衡。

#### 主程序句柄

`cdl_open(NULL, ...)` 使用：

```c
GetModuleHandleExW(0, NULL, &module);
```

取得当前可执行文件的模块句柄并增加引用计数。`cdl_close` 会 `FreeLibrary` 平衡。

注意：Windows 的 `GetProcAddress` 只查找导出表。普通 EXE 默认不导出内部函数，因此 `cdl_open(NULL)` 在 Windows 上通常不能像 POSIX 那样查找主程序中的全局符号。

#### 符号名

`GetProcAddress` 接收 `LPCSTR`，即 ANSI 名称。cdl 不把符号名转换为宽字符。导出名请使用 ASCII/ANSI 兼容名称，例如：

```c
__declspec(dllexport) int plugin_init(void);
```

#### 错误文本

Windows 系统错误通过 `FormatMessageW` 获取，再转换为 UTF-8。消息语言取决于系统 UI 语言。示例：

```text
LoadLibraryW failed (Windows error 126):
找不到指定的模块。
```

#### 不支持 `CDL_NODELETE` 的原因

Windows 没有与 `RTLD_NODELETE` 完全对应的“关闭后仍保持已初始化状态”的通用开关。虽然可以用 `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, ...)` 固定模块，但：

- 需要额外的模块路径解析；
- 与引用计数、`CDL_NOLOAD` 组合时语义复杂；
- 固定是不可逆的，和 POSIX 参考计数模型不完全相同。

因此 cdl 选择明确返回 `CDL_ERROR_NOT_SUPPORTED`，而不是静默改变行为。

---

## 8. 错误处理

### 8.1 错误状态生命周期

每个线程有独立错误状态：

```c
typedef struct cdl_error_state {
    cdl_error_t code;
    char message[1024];
} cdl_error_state;
```

规则：

1. `cdl_open`、`cdl_sym`、`cdl_close` 进入时清除本线程旧错误；
2. 操作成功时错误状态保持 `CDL_ERROR_NONE`；
3. 操作失败时设置错误码和消息；
4. `cdl_error()`、`cdl_last_error()` 只读取，不清除；
5. `cdl_error_clear()` 显式清除；
6. 其他线程不会读取或修改本线程的错误状态。

### 8.2 推荐的检查方式

```c
cdl_handle_t handle = cdl_open(path, flags);
if (handle == NULL) {
    fprintf(stderr, "open failed: code=%d message=%s\n",
            (int)cdl_last_error(),
            cdl_error() != NULL ? cdl_error() : "(none)");
    return -1;
}
```

对于 `cdl_sym`：

```c
void *symbol = cdl_sym(handle, name);
if (symbol == NULL) {
    cdl_error_t code = cdl_last_error();
    if (code == CDL_ERROR_NONE) {
        /* POSIX：符号存在且值为 NULL */
    } else {
        /* 真正失败 */
    }
}
```

### 8.3 错误消息的限制

- 消息缓冲区固定为 1024 字节；超长路径/系统消息可能被截断。
- 消息文本可能因操作系统语言而变化。
- 不要依赖错误消息的具体措辞。
- 程序逻辑应使用 `cdl_last_error()`。

### 8.4 错误码是否稳定？

枚举值在 1.x 系列中保持稳定。错误消息文本不保证稳定。新增错误码只会追加，不会改变已有值。

---

## 9. 实现细节

### 9.1 源文件分工

```text
include/cdl/cdl.h       公共 API、平台宏、导出宏
src/cdl.c               参数校验、错误状态、TLS、公共入口
src/cdl_internal.h      内部声明、平台后端接口
src/cdl_posix.c         Linux / Android 后端
src/cdl_windows.c       Windows 后端
tests/test_cdl.c        测试主程序
tests/test_module.c     被加载的测试模块
examples/load_math.c    示例
```

内部接口：

```c
cdl_handle_t cdl__platform_open(const char *path, unsigned int flags);
void *cdl__platform_sym(cdl_handle_t handle, const char *name);
int cdl__platform_close(cdl_handle_t handle);
```

公共入口负责：

- 清除旧错误；
- 校验 flags；
- 校验 `handle` / `name`；
- 调用平台后端；
- 保证错误状态一致。

平台后端负责：

- 映射 cdl flags 到系统 flags；
- 调用系统 API；
- 把系统错误转换为 cdl 错误状态；
- 处理平台特有的路径/编码/引用计数。

### 9.2 线程局部错误状态

`CDL_TLS` 宏按编译器选择：

```c
#if defined(_MSC_VER)
#  define CDL_TLS __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#  define CDL_TLS __thread
#else
#  error "cdl needs compiler support for thread-local storage"
#endif
```

错误状态定义：

```c
CDL_TLS cdl_error_state cdl__error_state = { CDL_ERROR_NONE, { 0 } };
```

这样：

- 多线程并发调用不会互相覆盖错误；
- 不需要锁；
- 不需要调用方传入错误缓冲区。

### 9.3 flags 校验

公共层先做与平台无关的检查：

```c
#define CDL_OPEN_FLAGS_KNOWN \
    ((unsigned int)(CDL_LAZY | CDL_NOW | CDL_GLOBAL | CDL_LOCAL | \
                    CDL_DEEPBIND | CDL_NODELETE | CDL_NOLOAD))

static int cdl__flags_valid(unsigned int flags)
{
    if ((flags & ~CDL_OPEN_FLAGS_KNOWN) != 0u)
        return 0;
    if ((flags & CDL_LAZY) != 0u && (flags & CDL_NOW) != 0u)
        return 0;
    if ((flags & CDL_GLOBAL) != 0u && (flags & CDL_LOCAL) != 0u)
        return 0;
    return 1;
}
```

平台后端再拒绝自己无法实现的标志。

### 9.4 POSIX 后端

关键点：

- 调用 `dlerror()` 清空旧错误；
- 调用 `dlopen` / `dlsym` / `dlclose`；
- 失败时立即读取 `dlerror()`，复制到 TLS 错误状态；
- `dlsym` 返回 `NULL` 但 `dlerror()` 为 `NULL` 时，表示符号值本身为 `NULL`，不设置错误。

### 9.5 Windows 后端

#### UTF-8 → UTF-16

1. 先用 `MultiByteToWideChar(..., NULL, 0)` 查询所需长度；
2. 检查整数溢出；
3. `malloc` 缓冲区；
4. 再调用一次转换；
5. 调用 `LoadLibraryW`；
6. 释放缓冲区。

失败路径会设置：

- `CDL_ERROR_INVALID_ARGUMENT`：非法 UTF-8、路径过长；
- `CDL_ERROR_OUT_OF_MEMORY`：分配失败。

#### Windows 错误文本

1. `FormatMessageW` 获取系统消息；
2. 去掉末尾的空白/换行；
3. `WideCharToMultiByte` 转为 UTF-8；
4. 复制到 TLS 错误缓冲区。

如果 `FormatMessageW` 失败，回退为：

```text
Windows error 126
```

#### 引用计数

- 正常加载：`LoadLibraryW` 增加引用计数；
- `CDL_NOLOAD`：`GetModuleHandleExW` 增加引用计数；
- 主程序句柄：`GetModuleHandleExW(0, NULL, ...)` 增加引用计数；
- `cdl_close`：`FreeLibrary` 减少引用计数。

因此每个成功的 `cdl_open` 都有对应的 `cdl_close`。

#### 符号地址转换

`GetProcAddress` 返回 `FARPROC`（函数指针）。cdl 使用 `memcpy` 把函数指针表示复制到 `void *`：

```c
FARPROC procedure = GetProcAddress(...);
void *symbol = NULL;
memcpy(&symbol, &procedure, sizeof(symbol));
```

文件内有静态断言，保证 `sizeof(FARPROC) == sizeof(void *)`。

### 9.6 导出与可见性

Windows：

```c
#if defined(CDL_STATIC)
#  define CDL_API
#elif defined(CDL_BUILDING)
#  define CDL_API __declspec(dllexport)
#else
#  define CDL_API __declspec(dllimport)
#endif
```

Linux/Android：

```c
#define CDL_API __attribute__((visibility("default")))
```

CMake 在共享库构建时：

- 定义 `CDL_BUILDING`；
- 设置 `C_VISIBILITY_PRESET hidden`（非 MSVC）；
- 只导出带 `CDL_API` 的公共符号。

静态库构建时：

- 定义 `CDL_STATIC`（PUBLIC），避免 Windows 消费者看到 `dllimport`；
- 不导出动态符号。

### 9.7 为什么不用 C++？

- 目标平台都提供 C API；
- 避免 C++ 运行时、异常、RTTI、STL 依赖；
- 方便嵌入纯 C 项目、内核工具链、游戏引擎和 NDK 项目；
- 减少 ABI 和名称修饰问题。

---

## 10. 测试与验证

### 10.1 测试内容

测试程序 `tests/test_cdl.c` 覆盖：

- 版本字符串；
- 非法 flags 组合；
- 空句柄、空符号名；
- 不存在模块；
- `cdl_open(NULL, ...)` 主程序句柄；
- 正常加载；
- 函数符号查找与调用；
- 数据符号读取；
- 不存在符号；
- `CDL_NOLOAD`；
- Windows 非法 UTF-8 路径；
- Windows 不支持 flags；
- Linux `CDL_DEEPBIND | CDL_NODELETE`；
- 错误状态清除与成功路径。

测试模块 `tests/test_module.c` 导出：

```c
int cdl_test_add(int a, int b);
int cdl_test_value;
const char *cdl_test_name(void);
```

### 10.2 Linux

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

直接运行：

```sh
./build/tests/cdl_tests ./build/tests/cdl_test_module.so
```

示例：

```sh
./build/examples/cdl_example
# 输出：cos(0) = 1.000000
```

### 10.3 Windows

MSVC：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

MinGW 交叉编译后可用 Wine 验证（仅开发环境）：

```sh
WINEDEBUG=-all wine build-win/tests/cdl_tests.exe \
  'Z:\path\to\build-win\tests\cdl_test_module.dll'
```

Windows 测试程序会通过 `CommandLineToArgvW` 读取 Unicode 命令行，再把路径转成 UTF-8 传给 cdl，因此构建目录包含中文等字符时也能正确测试。

### 10.4 Android

Android 测试可编译，但不会注册为 CTest。手动运行示例：

```sh
adb shell mkdir -p /data/local/tmp/cdl
adb push build-android/libcdl.so /data/local/tmp/cdl/
adb push build-android/tests/cdl_tests /data/local/tmp/cdl/
adb push build-android/tests/cdl_test_module.so /data/local/tmp/cdl/
adb shell chmod +x /data/local/tmp/cdl/cdl_tests

adb shell 'cd /data/local/tmp/cdl && \
  LD_LIBRARY_PATH=. ./cdl_tests ./cdl_test_module.so'
```

如果要构建 Android 测试：

```sh
-DCDL_BUILD_TESTS=ON -DCDL_BUILD_EXAMPLES=ON
```

### 10.5 已验证组合

- Linux x86_64，GCC 12，共享库/静态库，CTest 通过；
- Windows x86_64，MinGW-w64 交叉编译，Wine 下共享库/静态库测试通过；
- Android NDK r26d：arm64-v8a 共享库、x86_64 静态库，测试/示例编译通过；
- CMake 安装后 `find_package` 和 `pkg-config` 消费验证通过。

Windows MSVC 构建命令已提供，但当前验证环境主要使用 MinGW-w64；如果使用 MSVC，请根据项目实际工具链重新跑一次测试。

---

## 11. 边界与未实现能力

### 11.1 `dladdr`

`dladdr` 用地址反查模块/符号名。Windows 没有完全等价的纯 C API，通常需要：

- `dbghelp.dll` + PDB；
- 或自己遍历模块并解析 PE 导出表。

这超出了“libdl 核心包装”的范围，因此未提供。

### 11.2 `dlvsym`

`dlvsym` 是 glibc 的符号版本查询接口，Windows/Android 没有对应物。未提供。

### 11.3 `RTLD_DEFAULT` / `RTLD_NEXT`

- `RTLD_DEFAULT` 表示全局作用域查找；
- `RTLD_NEXT` 依赖调用点的返回地址，用于“查找当前符号之后的定义”。

如果把 `dlsym(RTLD_NEXT, ...)` 包进 `cdl_sym`，返回地址会变成 cdl 内部，语义就会改变。因此不提供透明包装。需要这类能力时，应直接在平台代码中调用 `dlsym`。

### 11.4 `dlinfo` / `dl_iterate_phdr` / `LD_AUDIT`

这些是平台/加载器特定接口，跨平台没有统一语义，未提供。

### 11.5 Windows 高级加载特性

- 按序号导入；
- DLL 重定向（`.local`、manifest）；
- 延迟加载（delay-load）；
- 自定义 LoadLibraryEx 标志；
- 应用容器/包身份相关加载策略。

cdl 使用标准 `LoadLibraryW`，不暴露这些扩展。

### 11.6 自动搜索路径

cdl 不会：

- 把相对路径转成绝对路径；
- 自动在可执行文件目录、当前目录、插件目录中查找；
- 模拟 Linux 的 `LD_LIBRARY_PATH` 到 Windows；
- 模拟 Windows 的 DLL 搜索顺序到 Linux。

搜索规则完全由系统加载器决定。需要可预测行为时，请使用绝对路径或显式配置系统搜索路径。

### 11.7 函数指针转换

C 标准不保证 `void *` 和函数指针可以自由互转。cdl 在所有目标平台上假设二者大小相同，并用 `memcpy` 做表示复制；测试中有静态断言。对于非主流 ABI（函数指针与数据指针大小不同），需要调整 API 或使用平台专用封装。

### 11.8 线程与卸载

cdl 不跟踪哪些线程正在执行某个模块的代码。调用方必须保证：

- 没有线程还在使用该模块时，才调用 `cdl_close`；
- 不要在模块的析构函数/静态销毁阶段回调正在卸载它的代码；
- 不要并发 `cdl_sym` 和 `cdl_close` 同一个句柄。

---

## 12. 故障排查

### 12.1 `cdl_open` 返回 `NULL`

先看：

```c
fprintf(stderr, "code=%d msg=%s\n",
        (int)cdl_last_error(),
        cdl_error() != NULL ? cdl_error() : "(none)");
```

#### Linux

检查：

```sh
ls -l /path/to/libfoo.so
file /path/to/libfoo.so
ldd /path/to/libfoo.so
```

常见原因：

- 路径错误；
- 依赖库缺失；
- 架构不匹配（x86_64 程序加载 arm64 库）；
- 符号未定义；
- 没有执行/读取权限；
- `RPATH`/`RUNPATH`/`LD_LIBRARY_PATH` 不符合预期。

#### Windows

错误消息会包含 Windows 错误码，常见：

| 错误码 | 含义 |
|---:|---|
| 126 | 找不到模块（或依赖模块） |
| 127 | 找不到过程/导出 |
| 193 | 不是有效的 Win32 应用（架构不匹配） |

检查：

```bat
dumpbin /dependents foo.dll
dumpbin /exports foo.dll
```

或使用 Dependencies / Process Monitor 查看 DLL 搜索过程。

常见原因：

- 32/64 位不匹配；
- VC++ 运行库缺失；
- DLL 依赖的其他 DLL 不在搜索路径；
- 路径编码不是 UTF-8；
- DLL 被安全软件拦截。

#### Android

检查：

- ABI 是否与设备匹配；
- `.so` 是否打进 APK 的正确 `jniLibs/<abi>`；
- API level 是否满足；
- 是否被 linker namespace 限制；
- 依赖的 `.so` 是否一起打包。

查看 logcat：

```sh
adb logcat | grep -i -E 'dlopen|linker|cdl'
```

### 12.2 `cdl_sym` 返回 `NULL`

- 用 `cdl_last_error()` 区分“符号值为 NULL”和“查找失败”；
- 检查符号是否导出：

```sh
nm -D --defined-only libfoo.so | grep symbol_name
```

```bat
dumpbin /exports foo.dll | findstr symbol_name
```

- C++ 插件是否使用 `extern "C"`；
- 是否被 `-fvisibility=hidden` 隐藏；
- Windows 是否使用 `__declspec(dllexport)`；
- 是否使用了符号版本（glibc）而名称不匹配；
- 是否把数据符号误当成函数符号调用。

### 12.3 Windows 静态链接报 `__imp_cdl_*`

原因：消费者把 cdl 当静态库链接，但头文件仍按 `dllimport` 展开。

解决：

- 使用 CMake target `cdl::cdl`（会自动带上 `CDL_STATIC`）；
- 或手工定义：

```c
#define CDL_STATIC
#include <cdl/cdl.h>
```

或在编译命令中加 `/DCDL_STATIC`。

### 12.4 Windows DLL 没有导出符号

构建 cdl DLL 时必须定义 `CDL_BUILDING`。使用 CMake 时会自动定义；手工编译时需要：

```bat
cl /DCDL_BUILDING /LD cdl.c cdl_windows.c /Iinclude /Fe:cdl.dll
```

### 12.5 `cdl_error()` 返回的字符串突然变了

错误消息是线程局部的，并且会在下一次 `cdl_open`、`cdl_sym`、`cdl_close` 或 `cdl_error_clear` 调用时被覆盖；`cdl_error()` 和 `cdl_last_error()` 本身不会覆盖它。若需要保留：

```c
char saved[256];
snprintf(saved, sizeof(saved), "%s", cdl_error());
```

不要保存 `cdl_error()` 返回的指针跨调用使用。

### 12.6 Android 上 `CDL_DEEPBIND` 失败

这是预期行为。bionic 不提供 `RTLD_DEEPBIND`。移除该标志，或使用平台专用加载逻辑。

### 12.7 Windows 上 `CDL_NODELETE` / `CDL_DEEPBIND` 失败

这是预期行为，见[平台行为详解](#7-平台行为详解)。如果确实需要固定 DLL，可自行调用：

```c
GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, path, &module);
```

但这会绕过 cdl 的引用计数模型，需自行管理。

### 12.8 MinGW 程序启动时报缺少 `libgcc_s_*.dll`

把对应 DLL 放到可执行文件旁边，或链接时加：

```sh
-static-libgcc
```

### 12.9 Windows 路径中有中文但加载失败

确认传给 `cdl_open` 的是 UTF-8，而不是 ANSI。`main()` 的 `argv` 在 Windows 上通常不是 UTF-8；使用 `wmain()` 或启用 UTF-8 活动代码页。

---

## 13. 版本与 ABI 约定

### 13.1 版本号

当前版本：

```text
1.0.0
```

CMake 项目版本和头文件宏保持一致：

```c
#define CDL_VERSION_MAJOR 1
#define CDL_VERSION_MINOR 0
#define CDL_VERSION_PATCH 0
#define CDL_VERSION_STRING "1.0.0"
```

运行时查询：

```c
printf("%s\n", cdl_version());
```

### 13.2 共享库 SONAME

Linux/Android 共享库设置：

```cmake
VERSION 1.0.0
SOVERSION 1
```

因此会生成：

```text
libcdl.so.1.0.0
libcdl.so.1
libcdl.so
```

主版本号变化表示可能存在 ABI 不兼容。

### 13.3 API 兼容策略

- 1.x 系列保持已有函数签名和枚举值不变；
- 新功能以新增函数/枚举值的方式加入；
- 错误消息文本不保证稳定；
- 平台支持范围可能随版本调整，但会记录在文档中；
- 结构体目前不暴露给调用方，避免 ABI 布局问题。

### 13.4 安装包版本匹配

```cmake
find_package(cdl 1.0 CONFIG REQUIRED)
```

`cdlConfigVersion.cmake` 使用 `SameMajorVersion` 兼容策略：请求 1.x 时接受 1.y。

---

## 14. License

MIT License。详见项目根目录 [LICENSE](../LICENSE)。

---

## 附录 A：头文件速查

```c
/* 平台宏 */
CDL_PLATFORM_WINDOWS
CDL_PLATFORM_LINUX
CDL_PLATFORM_ANDROID

/* 版本 */
CDL_VERSION_MAJOR
CDL_VERSION_MINOR
CDL_VERSION_PATCH
CDL_VERSION_STRING

/* 类型 */
cdl_handle_t
cdl_open_flag_t
cdl_error_t

/* 标志 */
CDL_LAZY
CDL_NOW
CDL_GLOBAL
CDL_LOCAL
CDL_DEEPBIND
CDL_NODELETE
CDL_NOLOAD

/* 错误码 */
CDL_ERROR_NONE
CDL_ERROR_INVALID_ARGUMENT
CDL_ERROR_OUT_OF_MEMORY
CDL_ERROR_NOT_SUPPORTED
CDL_ERROR_OPEN_FAILED
CDL_ERROR_SYMBOL_NOT_FOUND
CDL_ERROR_CLOSE_FAILED

/* 函数 */
cdl_open()
cdl_sym()
cdl_close()
cdl_error()
cdl_last_error()
cdl_error_clear()
cdl_version()
```

## 附录 B：平台选择速查

```c
#if defined(CDL_PLATFORM_WINDOWS)
    const char *plugin = "plugins\\audio.dll";
#elif defined(CDL_PLATFORM_ANDROID)
    const char *plugin = "libaudio.so";
#elif defined(CDL_PLATFORM_LINUX)
    const char *plugin = "./libaudio.so";
#else
#  error unsupported platform
#endif
```

## 附录 C：最小错误处理模板

```c
#include <cdl/cdl.h>
#include <stdio.h>

static int fail(const char *what)
{
    const char *message = cdl_error();
    fprintf(stderr, "%s failed: %s (code %d)\n",
            what,
            message != NULL ? message : "unknown error",
            (int)cdl_last_error());
    return -1;
}

int main(void)
{
    cdl_handle_t handle = cdl_open("libexample.so", CDL_NOW);
    if (handle == NULL)
        return fail("cdl_open");

    void *symbol = cdl_sym(handle, "example_init");
    if (symbol == NULL && cdl_last_error() != CDL_ERROR_NONE) {
        (void)cdl_close(handle);
        return fail("cdl_sym");
    }

    (void)cdl_close(handle);
    return 0;
}
```
