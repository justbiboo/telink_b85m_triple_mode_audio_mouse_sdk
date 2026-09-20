
## V0.2.6

### Version
* SDK version:telink_B85m_triple_mode_audio_mouse_sdk_V0.2.6
* BLE SDK Version: tc_ble_single_sdk V3.4.2.8
* Chips Version
  - Dongle: (B80: TLSR8373)
  - Mouse:  (B85: TLSR825X)
* Hardware Version
  - B80: C1T261A3_V1_1
  - B85: C1T159A6_V1_0
* Platform Version
  - tc_platform_sdk V3.3.0
* Toolchain Version
  - TC32 ELF GCC4.3 ( IDE: [Telink IoT Studio](https://www.telink-semi.com/development-tools ))


	

### Bug Fixes
* Fixed the issue that the mouse can't move fast normally  after turn on mic
* Fixed the issue that the mouse will automatically wake it up in ble mode when the Chromebook PC enters sleep 
* Fixed the issue that some specially operating system computers cannot enter sleep when mouse turn on mic
* Fixed the issue that dongle CV Test certification cannot be passed
* Fixed the issue that the mouse LED light behaves abnormally and the mouse cannot reconnect to the phone during the OTA upgrade process after the Bluetooth on the phone is turned off and then immediately turned back on
* Fixed the issue that the mouse cannot be used normally in BLE mode on some pc devices when open the mic
* Fixed the issue that the squeezed-out mouse will not enter the reconnect state when dongle is paired and connected with other mouse
* Fixed the issue that the button will automatically release if you pressed the mouse button for more than 3 seconds in 2.4g mode
* Fixed the issue that the mouse becomes unmovable and there are abnormalities on the dongle side when open mic in some PCs
* Fixed the issue that a pairing pop-up window appears on Windows computers when the mouse in reconnecting state


### BREAKING CHANGES 
* N/A


### Features
* Use msbc encoding
* Dongle/Mouse supports emi test
* Support usb mode


### Refactoring
* N/A


### Performance Improvements
* N/A

### Known issues
* N/A

### CodeSize
* B85_triple_mode_mic_mouse
  - Firmware size: 91.6 kBytes
  - SRAM size: 29.8 kBytes
* dongle
  - Firmware size: 20.2 kBytes
  - SRAM size: 11.3 kBytes


### Version
* SDK 版本:telink_B85m_triple_mode_audio_mouse_sdk_V0.2.6
* BLE SDK 版本: tc_ble_single_sdk V3.4.2.8
* Chip 版本
  - Dongle: (B80: TLSR8373)
  - Mouse:  (B85: TLSR825X)
* Hardware Version
  - B80: C1T261A3_V1_1
  - B85: C1T159A6_V1_0
* Platform 版本
  - tc_platform_sdk V3.3.0
* Toolchain 版本
  - TC32 ELF GCC4.3 ( IDE: [Telink IoT Studio](https://www.telink-semi.com/development-tools ))



### Bug Fixes
* 修复鼠标开启语音模式后，鼠标快速划水平线/垂直线/45度对角线，会出现画不动线、有折线现象的问题
* 修复Chromebook上，让PC进入睡眠状态，鼠标会自动唤醒PC的问题
* 修复部分操作系统电脑上，鼠标打开语音模式，电脑无法进入睡眠状态的问题
* 修复dongle CV Test认证无法通过的问题
* 修复鼠标OTA升级过程中，关闭手机蓝牙后立刻再打开蓝牙，鼠标LED灯异常且鼠标无法回连上手机的问题
* 修复ble模式在部分设备上鼠标打开语音功能后无法使用的问题
* 修复dongle与多个鼠标配对连接，被挤掉的鼠标不会进入回连状态的问题
* 修复2.4g 模式长按鼠标按键超过3s，按键会自动释放的问题
* 修复鼠标在部分系统的PC上开启语音模式后出现无法使用，dongle端出现异常的问题
* 修复鼠标回连时windows系统电脑会弹出配对弹窗的问题

### BREAKING CHANGES 
* N/A

### Features
* 使用 msbc 编码
* dongle 和鼠标增加emi 测试功能
* 增加usb 模式



### Refactoring
* N/A

### Performance Improvements
* N/A

### Known issues
* N/A

### CodeSize
* B85_triple_mode_mic_mouse
  - Firmware size: 91.6 kBytes
  - SRAM size: 29.8 kBytes
* dongle
  - Firmware size: 20.2 kBytes
  - SRAM size: 11.3 kBytes


## V0.2.0

### Version
* SDK version:telink_B85m_triple_mode_audio_mouse_sdk_V0.2.0
* BLE SDK Version: tc_ble_single_sdk V3.4.2.8
* Chips Version
  - Dongle: (B80: TLSR8373 B80B: TLSR8208 Version B)
  - Mouse:  (B85: TLSR825X B87: TLSR827X)
* Platform Version
  - tc_platform_sdk V3.3.0
* Toolchain Version
  - TC32 ELF GCC4.3 ( IDE: [Telink IoT Studio](https://www.telink-semi.com/development-tools ))


	

### Bug Fixes
* Fixed the issue that the mouse will be disconnected after turn off mic when the mouse connected with the iOS phone 

### BREAKING CHANGES 
* N/A


### Features
* Use msbc encoding
* Dongle supports OTP
* Support flash lock and unlock funtion


### Refactoring
* N/A


### Performance Improvements
* N/A

### Known issues
* N/A

### CodeSize
* B85_dual_mode_mic_mouse
  - Firmware size: 88.6 kBytes
  - SRAM size: 29.2 kBytes
* B87_triple_mode_mic_mouse
  - Firmware size: 92.3 kBytes
  - SRAM size: 30.6 kBytes
* dongle
  - Firmware size: 20.0 kBytes
  - SRAM size: 11.3 kBytes


### Version
* SDK 版本:telink_B85m_triple_mode_audio_mouse_sdk_V0.2.0
* BLE SDK 版本: tc_ble_single_sdk V3.4.2.8
* Chip 版本
  - Dongle: (B80: TLSR8373 B80B: TLSR8208 Version B)
  - Mouse:  (B85: TLSR825X B87: TLSR827X)
* Platform 版本
  - tc_platform_sdk V3.3.0
* Toolchain 版本
  - TC32 ELF GCC4.3 ( IDE: [Telink IoT Studio](https://www.telink-semi.com/development-tools ))



### Bug Fixes
* 修复鼠标连上ios 手机开语音后关语音会断连的问题

### BREAKING CHANGES 
* N/A

### Features
* 使用 msbc 编码
* dongle 支持otp
* 支持flash锁功能



### Refactoring
* N/A

### Performance Improvements
* N/A

### Known issues
* N/A

### CodeSize
* B85_dual_mode_mic_mouse
  - Firmware size: 88.6 kBytes
  - SRAM size: 29.2 kBytes
* B87_triple_mode_mic_mouse
  - Firmware size: 92.3 kBytes
  - SRAM size: 30.6 kBytes
* dongle
  - Firmware size: 20.0 kBytes
  - SRAM size: 11.3 kBytes


