# SDL Agent — Android

手机使用抽屉式会话列表，平板和桌面保持左右分栏。自绘标题栏在 Android 上隐藏，
UI 缩放跟随系统 DPI。

## 依赖位置

Android 工程只提交 Gradle / CMake 配置，第三方源码和交叉编译产物放在仓库外。
默认查找 `<仓库目录>/../android_deps`：

```text
<deps-dir>
├─ SDL          3.4.20
├─ SDL_ttf      3.2.2（含 external/freetype、external/harfbuzz 子模块）
├─ curl         8.23.0
├─ mbedtls      3.6.8
├─ cjson        1.7.19
├─ md4c         0.6.0
└─ prefix/arm64-v8a
   ├─ include
   └─ lib         libcurl.a / libmbedtls.a / ...
```

如果目录在别处，构建前设置：

```powershell
$env:SDLAGENT_DEPS = "<deps-dir>"
```

`prefix/arm64-v8a` 由 mbedTLS 和 libcurl 交叉编译产生。
SDL3 和 SDL3_ttf 由 Gradle 的 CMake 构建直接编译。

## 环境变量

```powershell
$env:JAVA_HOME    = "<path-to-jdk-17>"
$env:ANDROID_HOME = "<path-to-android-sdk>"
```

`android/local.properties` 不在版本库里，需要自己创建（路径使用 `/`）：

```properties
sdk.dir=<path-to-android-sdk>
```

## 构建

```bash
cd android
./gradlew assembleDebug    # 调试包
./gradlew assembleRelease  # 签名发布包
```

APK：

```text
android/app/build/outputs/apk/debug/app-debug.apk
android/app/build/outputs/apk/release/app-release.apk
```

发布包使用 `android/keystore.properties` 指向的签名（该文件和 keystore 目录
都被 git 忽略）。没有这两项时 `assembleRelease` 会产出未签名 APK。

`assets/api.json` 会在构建时复制进 APK 的 `assets/`。Android 首次启动时，
如果数据目录里没有 `api.json`，会从 APK 资源中复制一份。

安装（`adb` 在 PATH 里时）：

```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

或使用 SDK 里的 `platform-tools/adb`。

当前只构建 `arm64-v8a`。需要模拟器时在 `app/build.gradle` 的 `abiFilters`
里加 `x86_64`，并用 NDK 重新编译 mbedTLS / curl 的 x86_64 版本。

## 尚未适配

- 附件选择、会话导出、日志导出：`SDL_ShowOpen/SaveFileDialog` 在 Android 上
  没有实现，需要 JNI 调 `ACTION_OPEN_DOCUMENT` / `ACTION_CREATE_DOCUMENT`
- 设置页仍是桌面尺寸布局，手机上内容会被裁切
- shell 工具只能调用 `/system/bin` 里的命令，App 私有目录不可执行
- 软键盘弹出时输入框的避让未处理
