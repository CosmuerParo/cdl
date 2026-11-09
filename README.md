# cdl

`cdl` 是一个用 **C99** 编写的小型动态库加载包装库，用一套 API 统一：

- Linux：`dlopen` / `dlsym` / `dlclose` / `dlerror`
- Android：bionic 的 `dlopen` 家族
- Windows：`LoadLibraryW` / `GetProcAddress` / `FreeLibrary` / `FormatMessageW`

项目只依赖 C 标准库和系统 API，不使用 C++。构建系统为 **CMake**，同时支持共享库、静态库、安装导出和 `pkg-config`。

> 详细使用手册（API、平台差异、错误处理、构建、测试、故障排查）：[docs/README.md](docs/README.md)

## 特性

- 统一句柄类型和错误接口，错误状态按线程保存。
- Windows 路径按 UTF-8 接收，内部转换为 UTF-16 调用宽字符 API。
- Windows 错误通过 `FormatMessageW` 转成 UTF-8，Linux/Android 使用 `dlerror()`。
- 支持 `CDL_LAZY`、`CDL_NOW`、`CDL_GLOBAL`、`CDL_LOCAL`、`CDL_DEEPBIND`、`CDL_NODELETE`、`CDL_NOLOAD`，不支持的平台明确返回错误。
- 提供 CMake `find_package(cdl CONFIG)` 目标 `cdl::cdl`，以及 `cdl.pc`。
- 包含 Linux / Windows / Android 可编译的测试和示例。

## 平台能力矩阵

| 能力 | Linux | Android | Windows |
|---|---|---|---|
| 加载 / 取符号 / 卸载 | `dlopen` 家族 | `dlopen` 家族 | `LoadLibraryW` 家族 |
| `CDL_LAZY` / `CDL_NOW` | 生效 | 生效 | 接受；Windows 没有延迟绑定，等价 |
| `CDL_GLOBAL` / `CDL_LOCAL` | 生效 | 生效 | 接受；Windows 导出是进程级可见，无局部/全局之分 |
| `CDL_NODELETE` | `RTLD_NODELETE` | bionic 支持时生效 | 不支持，返回 `CDL_ERROR_NOT_SUPPORTED` |
| `CDL_NOLOAD` | `RTLD_NOLOAD` | bionic 支持时生效 | `GetModuleHandleExW` |
| `CDL_DEEPBIND` | glibc 上生效 | 不支持，返回错误 | 不支持，返回错误 |
| 路径编码 | 原生字节串 | 原生字节串 | UTF-8，转换为 UTF-16 |
| 错误文本 | `dlerror()` | `dlerror()` | `FormatMessageW`，转换为 UTF-8 |

> Windows 的 `CDL_LAZY`、`CDL_NOW`、`CDL_GLOBAL`、`CDL_LOCAL` 会被接受但不会改变加载行为。这是为了让同一份调用代码可以跨平台编译；真正无法等价实现的 `CDL_DEEPBIND` 和 `CDL_NODELETE` 会明确失败，而不是被静默忽略。

## 构建

### Linux

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

### Windows（MSVC）

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix C:\path\to\install
```

### Windows（MinGW-w64 交叉编译）

```sh
cmake -S . -B build-win -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```

MinGW 默认动态链接 `libgcc_s_*.dll`（64 位 SEH 版本是 `libgcc_s_seh-1.dll`）；部署时请把它和程序一起发布，或者在链接阶段使用 `-static-libgcc`。

### Android（NDK）

```sh
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android
```

静态库：

```sh
-DCDL_BUILD_SHARED=OFF
```

Android 上的测试可编译，但不会在 CMake 中注册为 CTest（因为测试需要设备或模拟器）；可把 `cdl_tests` 和 `cdl_test_module.so` 推到设备上手动运行。

## CMake 选项

| 选项 | 默认值 | 说明 |
|---|---|---|
| `CDL_BUILD_SHARED` | `ON` | `ON` 构建共享库，`OFF` 构建静态库 |
| `CDL_BUILD_TESTS` | 顶层构建时为 `ON` | 构建测试 |
| `CDL_BUILD_EXAMPLES` | 顶层构建时为 `ON` | 构建示例 |
| `CDL_INSTALL` | 顶层构建时为 `ON` | 生成安装和包配置 |

作为子项目使用：

```cmake
add_subdirectory(third_party/cdl)
target_link_libraries(my_app PRIVATE cdl::cdl)
```

## 使用

### CMake 包

安装后：

```cmake
find_package(cdl 1.0 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE cdl::cdl)
```

### pkg-config

```sh
cc app.c $(pkg-config --cflags --libs cdl) -o app
```

静态链接时：

```sh
cc app.c $(pkg-config --cflags --libs --static cdl) -o app
```

### 基本用法

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
        cdl_close(handle);
        return 1;
    }

    /* C 标准不保证 void* 和函数指针可以互相转换，使用 memcpy 更稳妥。 */
    memcpy(&answer, &symbol, sizeof(answer));
    printf("answer = %d\n", answer());

    cdl_close(handle);
    return 0;
}
```

Windows 上把库名换成 `.dll`，例如 `"example.dll"`。

## API

```c
cdl_handle_t cdl_open(const char *path, unsigned int flags);
void        *cdl_sym(cdl_handle_t handle, const char *name);
int          cdl_close(cdl_handle_t handle);

const char  *cdl_error(void);
cdl_error_t  cdl_last_error(void);
void         cdl_error_clear(void);
const char  *cdl_version(void);
```

约定：

- `cdl_open(NULL, flags)` 打开主程序，等价于 POSIX 的 `dlopen(NULL, ...)`。Windows 上它返回可执行模块句柄，只能通过 `GetProcAddress` 查到该 EXE 的导出符号。
- `flags == 0` 等价于 `CDL_NOW | CDL_LOCAL`。
- `CDL_LAZY`/`CDL_NOW` 和 `CDL_GLOBAL`/`CDL_LOCAL` 是互斥的；同时给出会返回 `CDL_ERROR_INVALID_ARGUMENT`。
- 每次调用 `cdl_open`、`cdl_sym`、`cdl_close` 都会先清除本线程的旧错误。
- `cdl_error()` 返回的字符串由库持有，有效期到同一线程下一次 `cdl_open`、`cdl_sym`、`cdl_close` 或 `cdl_error_clear` 调用为止，不要 `free`。
- `cdl_sym()` 返回 `NULL` 且 `cdl_last_error() == CDL_ERROR_NONE` 时，表示符号值为空（POSIX 数据符号允许）；Windows 上 `NULL` 一定是失败。
- 成功打开的句柄必须调用一次 `cdl_close()`。
- 所有 API 可跨线程使用；错误状态是每线程独立的。

Windows 上 `cdl_open()` 接收 UTF-8 路径。如果路径来自 `main()` 的 `argv`，注意 MSVC/MinGW CRT 通常按当前 ANSI 代码页提供 `argv`，并不保证是 UTF-8；此时应改用 `wmain()` 后自行转成 UTF-8，或为程序启用 UTF-8 活动代码页。

## 实现说明与边界

`cdl` 只封装“加载库、取符号、卸载库、取错误”这组核心能力。以下内容有意不提供：

- `dladdr`：Windows 需要调试符号/`dbghelp`，不属于纯 `libdl` 等价能力。
- `dlvsym`：仅 glibc 提供，Windows/Android 无对应物。
- `RTLD_DEFAULT` / `RTLD_NEXT`：`RTLD_NEXT` 依赖调用点的返回地址，包在另一个函数里会改变语义，无法透明包装。
- Windows 的按序号导入、重定向等 PE 特性：`GetProcAddress` 只按名称查找导出符号。
- Windows 符号名按 `GetProcAddress` 的 ANSI 名称处理；路径则完整支持 UTF-8 到 UTF-16。

Windows 端 `cdl_open` 使用 `LoadLibraryW` 的默认搜索规则；Linux/Android 使用 `dlopen` 的搜索规则。`cdl` 不会自行模拟另一平台的搜索路径。

## 目录结构

```text
include/cdl/cdl.h       公共 API
src/cdl.c               通用参数校验、错误状态、线程局部存储
src/cdl_posix.c         Linux / Android 后端
src/cdl_windows.c       Windows 后端（UTF-8/UTF-16、FormatMessage）
tests/                  测试模块和测试程序
examples/load_math.c    加载 libm/msvcrt 并调用 cos 的示例
cmake/                  CMake 包配置和 pkg-config 模板
docs/README.md          详细使用手册
```

## 已验证

- Linux x86_64：GCC 12，共享库/静态库，CTest 通过。
- Windows x86_64：MinGW-w64 交叉编译，Wine 下共享库/静态库测试通过。
- Android：NDK r26d，arm64-v8a 共享库、x86_64 静态库，以及测试/示例均编译通过（未在设备上运行）。

## License

MIT，见 [LICENSE](LICENSE)。
