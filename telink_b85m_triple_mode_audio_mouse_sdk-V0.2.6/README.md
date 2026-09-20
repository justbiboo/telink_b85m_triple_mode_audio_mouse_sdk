# telink_B85m_triple_mode_audio_mouse_sdk

triple mode audio mouse
## Version
* SDK版本更新到V0.2.6

### Features
* (B85_mouse) 修复鼠标EMI模式，TX模式没有波形的问题
* (B80_dongle) dongle无法被EMI上位机工具识别的问题

### BREAKING CHANGES
* N/A 

## Version
* SDK版本更新到V0.2.5.6

### Features
* (B85_mouse) 更改BLE鼠标OTA升级灯效为成功灯效为长亮2s, 失败为快闪两次

### BREAKING CHANGES
* N/A 
## Version
* SDK版本更新到V0.2.5.5

### Features
* (B85_mouse) 修复mouse_BLE)鼠标OTA升级过程中，关闭手机蓝牙后立刻再打开蓝牙，鼠标LED灯异常且鼠标无法回连上手机的问题

### BREAKING CHANGES
* N/A 

## Version
* SDK版本更新到V0.2.5.4

### Features
* (B85_mouse) 修复usb 模式在Ubuntu（Linux系统）电脑和Chromebook上，鼠标开启语音功能后让PC进入睡眠状态，无法进入睡眠状态的问题
* (B85_mouse) 修复USB模式：win10（LENOVO（拯救者R7000p））和MAC系统上，鼠标打开语音功能，然后让电脑进入睡眠，通过鼠标无法唤醒PC的问题

### BREAKING CHANGES
* N/A 


## Version
* SDK版本更新到V0.2.5.3

### Features
* (B85_mouse) usb 模式改为不打开语音报告率1K, 打开语音500
* (B85_mouse) 修复2.4G模式/USB模式：开启语音模式后，快速划水平线/垂直线/45度对角线，会出现画不动线、折线现象

### BREAKING CHANGES
* N/A 


## Version
* SDK版本更新到V0.2.5.2

### Features
* (B85_mouse) usb 模式降低语音poll rate
* (B85_mouse) ble 模式改为自定义服务上报语音数据，修复了在mac 电脑上开语音画不动线的问题
* (B85_mouse) ble 语音数据传输时增加fifo大小判断，并对ui间隔根据连接间隔进行了动态更改
* (B85_mouse) 修复ble 模式鼠标重新进入配对状态时鼠标指针漂动的问题
* (B85_mouse) 修复ble 模式多通道配对同一台电脑出现已连接已断开不断切换无法使用的问题
* (B85_mouse) 修复ble Chromebook上，让PC进入睡眠，PC息屏后鼠标自动唤醒PC的问题
* (B85_mouse) 修改蓝牙名称为AI_58M
* (B80_dongle) mouse 使用单独的fifo, 避免因fifo 堵塞 在linux 系统上开语音无法工作的问题

### BREAKING CHANGES
* N/A 


## Version
* SDK版本更新到V0.2.5.1

### Features
* (B85_mouse) 修复ble模式回连会弹窗的问题
* (B85_mouse) 修复dongle与多个鼠标配对连接，被挤掉的鼠标不会进入回连状态的问题
* (B85_mouse) 修复长按鼠标按键超过3s，按键会自动释放的问题
* (B80_dongle) 修复第一个鼠标与dongle配对连接后保持静置，第二个鼠标进入配对模式，不拔插dongle也能配对连接的问题
* (B80_dongle) 修复dongle hid 测试描述符获取不到的问题
* (B80_dongle) 修复dongle hid 测试report 描述符范围不通过的问题
### BREAKING CHANGES
* N/A 

## Version
* SDK版本更新到V0.2.5

### Features
* (B85_mouse) 增加usb 收包打印功能
* (B85_mouse) ble 改为通过usb hid 方式上报语音数据，方便测试语音质量
* (B85_mouse) rf 中断取消全清中断标志的处理
* (B85_mouse) 2.4g 增加低功耗处理
* (B85_mouse) 根据实际测量值设置鼠标相关报警电压
* (B85_mouse) ble 模式关闭打印灯设为绿灯，同时电压检测使能io口在测完后关闭输出

### BREAKING CHANGES
* N/A 

## Version
* SDK版本更新到V0.2.4

### Features
* (B85_mouse) 修复usb ota 不成功的bug

### BREAKING CHANGES
* N/A 


## Version
* SDK版本更新到V0.2.3

### Features
* (B85_mouse) 修复不能同时上报多个按键的bug
* (B85_mouse) 增加按下cpi 键时 三种模式上报01 的自定义值功能
* (B85_mouse) 根据公版三模硬件实现并验证画线正常，其中一个硬件画线画不动
* (B85_mouse) 修改默认报告率为250，打开语音125
* (B85_mouse) usb 模式增加pc 睡眠唤醒功能

### BREAKING CHANGES
* N/A 


## Version
* SDK版本更新到V0.2.2

### Features
* (B85_mouse) 按照公版三模硬件实现左中右按键，K4,K5键和滚轮功能
* (B85_mouse) 按照公版三模硬件实现语音键(K5)键按下上报语音数据，再次按下关语音
* (B85_mouse) 根据公版三模硬件实现并验证画线正常，其中一个硬件画线画不动
* (B85_mouse) 实现公版三模硬件底部按键开关往上2.4g模式， 往下 ble 模式， 中间插上usb 进入usb 模式
* (B85_mouse) 公版三模硬件实现对应模式灯效
* (B85_mouse) 增加移动唤醒和电压检测功能
* (B85_mouse) 增加cpi 检测和按键切换cpi 功能
* (B85_mouse) 对硬件语音质量底噪大的问题进行排查后硬件加上1uf 电容后有明显改善
* (B85_mouse) 对硬件底部开关上拉切换不到2.4g 模式的问题进行排查发现硬件电阻焊反了，重新焊接后可以正常检测
* (B85_mouse) 对硬件rf 差， 2.4g 配对不上，蓝牙很难连上的问题硬件添加匹配模块后rf 正常可以正常配对连接使用
* (B85_mouse) sensor 方向改为6点钟方向

### BREAKING CHANGES
* N/A 

## Version
* SDK版本更新到V0.2.1

### Features
* (B85_mouse) 按照客户三模硬件实现左中右按键，K4,K5键功能
* (B85_mouse) 按照客户三模硬件实现语音键按下上报语音数据，再次按下关语音
* (B85_mouse) 根据客户三模硬件实现并验证画线正常
* (B85_mouse) 根据客户三模硬件增加底部按键切换三种模式功能
* (B85_mouse) 根据客户三模硬件实现对应模式灯效
* (B85_mouse) usb 模式增加usb 状态检测，客户三模硬件usb 枚举不成功，mic 收音全是杂音，无5v 检测口


### BREAKING CHANGES
* N/A
### Version
* SDK版本更新到V0.1.0

### Features
* (B85_mouse) 实现基础左中右按键，K4,K5键功能
* (B85_mouse) sensor 方向改为12点钟方向，2.4g 开语音上报率250，关语音上报率1000
* (B85_mouse) 2.4g 增加audio adpcm 格式数据上报和上位机指令接收功能
* (B85_mouse) 2.4g 增加ai 键打开语音上报自定义值01,关闭语音上报自定义值03
* (B85_mouse) ble 增加通过uuid 0xB03E 通道上报语音数据
* (B85_mouse) 实现模式切换键切换模式，2.4g 亮绿灯， ble 模式亮蓝灯
* (B85_mouse) audio自适应控制功能，根据电量检测，百分之七十以上电量一次开语音时长限制为1分钟，百分之40—70, 限制到30s,40以下10s

### BREAKING CHANGES
* N/A

### Version
* SDK版本更新到V0.0.4

### Features
* (mouse) 2.4g增加自定义协议上报，开语音键发01，关发03
* (mouse) 增加app cmd
* (mouse) 增加收到dongle 有上位机数据接收打印并回ack 功能
* (mouse) 增加audio自适应控制功能，根据电量检测，百分之七十以上电量一次开语音时长限制为1分钟，百分之40—70, 限制到30s,40以下10s
* (dongle) 增加rx fifo 和app cmd
* (dongle) 增加usb set out report 接收上位机数据，增加上位机数据填充fifo, 回发mouse
* (dongle) 增加 上位机数据告知鼠标和接收到鼠标ack 更新fifo
* (dongle) 增加自定义report id 0b 的app 数据报告描述符
* (dongle) 打开usb ota 和端点4，通过端点4上报鼠标端app数据
* (dongle) 更改vid 为248a, product string 为 Telink Audio
* (dongle) 对有上位机数据时和无上位机数据时回包长度进行了区分，有的情况回长度7，没有回长度1



### BREAKING CHANGES
* N/A


### Version
* SDK版本更新到V0.0.3

### Features
* (mouse) 增加语音包索引，避免丢包后数据紊乱
* (dongle) 增加语音包索引，避免数据紊乱



### BREAKING CHANGES
* N/A

### Version
* SDK版本更新到V0.0.2

### Features
* (mouse) 上报语音数据时取消进行整包比较
* (mouse) 修复2.4g 睡眠起来tick 更新问题



### BREAKING CHANGES
* N/A

### Version
* SDK版本更新到V0.0.1

### Features
* (mouse) 支持audio demo, 使用adpcm 编解码方式传输
* (mouse) 蓝牙自定义数据服务上传音频数据
* (mouse) 支持usb 模式， 枚举成ai office mic，可以上传音频和鼠标数据
* (mouse) 语音功能开关由voice 键控制，按一次开，再按一次关
* (mouse) 打开语音模式下进行MTU size exchange
* (mouse) 2.4g 模式 通过语音通道上传数据给dongle
* (dongle) 支持adpcm 解码，收到鼠标上传的音频数据后同步音频流
* (dongle) 枚举成ai office 音频设备,支持鼠标数据上传的同时可以直接通过音频播放器播放音频



### BREAKING CHANGES
* N/A