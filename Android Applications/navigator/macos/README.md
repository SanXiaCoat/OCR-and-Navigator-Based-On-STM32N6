# macOS 导航 JSON 接收端

这个接收端使用 BLE，让 Android App 的“连接 Mac”按钮可以连接到 macOS 并发送导航 JSON。收到的数据会按一行一个 JSON 写入桌面日志文件。

## 运行

推荐使用 `.app` 方式运行，这样 macOS 会给接收端自己的蓝牙权限，而不是复用 Terminal 的权限。

在项目根目录执行：

```bash
zsh macos/build_receiver_app.sh
open macos/NaviJsonMac.app
```

首次运行时，macOS 可能会弹出蓝牙权限提示，请允许 NaviJsonMac 使用蓝牙。

接收端日志在桌面：

```bash
tail -f ~/Desktop/NaviJsonMac.log
```

看到下面提示后，再到 Android App 点击“连接 Mac”：

```text
NaviJsonMac is advertising. Tap "连接 Mac" in the Android app.
```

如果需要继续用脚本调试，也可以执行：

```bash
swift macos/BleNaviReceiver.swift
```

## 接收格式

Android 侧会发送以换行符分隔的 JSON，例如：

```json
{"type":"navi_update","text":"前方右转","remainDistance":1200,"remainTime":300,"curRoad":"人民路","nextRoad":"建设路","iconType":2,"timestamp":1717167600000}
```

`type` 可能是：

- `navi_update`：实时导航状态。
- `navi_text`：高德导航播报文字。

## 注意

- “连接蓝牙”按钮仍用于经典蓝牙 SPP 设备，例如 Android 手机串口工具或 HC-05/HC-06。
- “连接 Mac”按钮用于这个 macOS BLE 接收端，不需要先在系统蓝牙设置里配对。
- 如果 Android 端提示找不到 Mac，请确认 Mac 蓝牙已开启、接收端仍在运行，并且终端 App 已获得蓝牙权限。
