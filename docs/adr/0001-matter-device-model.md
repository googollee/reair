---
type: adr
status: 生效
review: ../review.md#adr
---

# 0001 Matter 设备模型

## 决策

每个节点作为一个 Matter 设备，包含以下端点，全部使用标准簇，不使用厂商自定义簇或属性：

| 端点 | 簇 | 内容 |
|---|---|---|
| 风扇 | Fan Control | 关闭与高、中、低三档风力 |
| Cycle | ModeSelect | 60 s、90 s、120 s |
| Role | ModeSelect | Leader、Follower、Independent |
| Forward Direction | ModeSelect | Intake、Exhaust，默认 Exhaust |
| Actual Direction | ModeSelect | Intake、Exhaust、Stopped，只读 |
| Warning | ModeSelect | None、Warning；只有节点能置为 Warning，控制器只能置为 None |
| Direction Mode | ModeSelect | Alternating（往复）、Forward（固定正向）、Reverse（固定反向） |

首次上电或没有保存的设置时，各端点的初始值为：角色 Independent、正向风向 Exhaust、风向模式 Alternating、往复周期 60 s、风力关闭、Warning 为 None。

### 显示文字

- 各 ModeSelect 的 Description 属性和选项标签使用英文，即上表中的名称，用于控制器中的显示。
- 角色的三个取值对应需求中的「主」「从」「独立」：Leader 为主，Follower 为从，Independent 为独立。

### 风力

- 风扇只有关闭、低、中、高四档，百分比对应 0、1～33、34～66、67～100。设置的百分比归入所在档位，风扇的百分比属性随之取该档的上限值 0、33、66、100。
- 不属于这四档的风扇模式（如自动）写入时返回错误。

### 写入限制

- Actual Direction 只读，控制器对它的切换一律返回错误。
- Direction Mode 的选项即风向模式：Alternating 按 Cycle 周期往复，Forward、Reverse 固定在对应的逻辑风向。
- Warning 由节点在发现多主时置为 Warning；控制器切换为 None 即清除警告，切换为 Warning 返回错误。
- 节点为 Follower 时，对风扇、Cycle 和 Direction Mode 的写入返回错误。风力由主节点决定；风向模式和 Cycle 保持节点自己的设置，不跟随主节点。

## 依据

- 每个节点都是独立的 Matter 设备：[F-002](../prd.md#f-002)。
- 角色、正向风向、风向模式、往复周期、风力的取值范围：[F-003](../prd.md#f-003)、[F-004](../prd.md#f-004)、[F-005](../prd.md#f-005)、[F-006](../prd.md#f-006)、[F-007](../prd.md#f-007)。
- 从节点不能修改风向、风向模式、往复周期和风力：[F-008](../prd.md#f-008)。
- 多主警告可查看并清除：[F-011](../prd.md#f-011)。
- 实际风向只读：[F-012](../prd.md#f-012)。
- 风向模式、周期和角色没有对应的标准属性，这些设置都是少数几个固定取值，适合用 ModeSelect 表示。
- Home Assistant 把 ModeSelect 实体归入配置类别，自动化可以直接选择选项，三选一的风向模式一步即可设定。
- Home Assistant 为任何带 ModeSelect 簇的端点创建下拉选择实体，实体名取自 Description 属性，选项标签取自 SupportedModes，设备端写什么就显示什么。
- ModeSelect 的取值只能通过 ChangeToMode 命令切换，所以只读和限制写入的做法是让命令返回错误；控制器不能直接写 CurrentMode。
- Matter 同一端点上同一个簇只能有一个实例，所以每个 ModeSelect 都需要独立的端点。

## 备选方案

### 厂商自定义簇或属性（未采纳）

可以表示任意取值。但主流控制器不保证显示，Home Assistant 官方文档也未说明对自定义簇的支持，用户可能无法设置。

### 借用 Level Control 表示往复周期（未采纳）

控制器会显示为调光器，名称和用途不符，数值范围也远大于实际需要。

### 用 OnOff 开关表示正向风向（未采纳）

Home Assistant 的开关实体只显示设备名，没有找到通用的自定义名称方式，用户看不到「正向风向」这个名字。

### 用 Fan Control 的 AirflowDirection 表示正向风向或实际风向（未采纳）

该属性含义是当前风向，不是风向定义。用于实际风向时，Home Assistant 风扇界面会出现可操作的方向按钮，而实际风向要求只读。

### 用 Boolean State 表示实际风向或多主警告（未采纳）

Home Assistant 只对 Contact Sensor 等特定设备类型创建对应的二进制传感器，没有通用实体，显示的含义也不符。

### 用 Operational State 表示实际风向和多主警告（未采纳）

- Matter SDK 的 Operational State 服务端固定声明 Pause、Stop、Start、Resume 四个命令，Home Assistant 为每个命令创建一个按钮，每个端点多出四个不可用的按钮。去掉需要修改 SDK。
- Home Assistant 的传感器名称是固定的「Operational state」，只用端点号区分，无法显示实际风向或多主警告。
- Home Assistant 只对 stopped、running、paused、error 等少数状态翻译，其他状态显示小写标识，同一个传感器的状态文字会中英混杂。

### 用 Identify 的命令按钮清除多主警告（未采纳）

Home Assistant 为命令创建的按钮名称固定，不能显示为「清除多主警告」，用户会困惑。

### 为清除多主警告单独设一个 ModeSelect 端点（未采纳）

选项为 None、Clear，选 Clear 后节点清除警告并把选项恢复为 None。多一个端点，控制器中出现两个与警告相关的实体，语义重复。

### 用 Fan Control 的摆动和方向表示风向模式（未采纳）

摆动是开关，三种风向模式要靠摆动加方向两个属性组合；自动化需要按顺序写两次。往复时对方向的写入，值与当前相同时 SDK 不会通知固件，用户点击没有反应。

### 借用其他电器的 Mode 簇或 Temperature Control 表示风向模式（未采纳）

这些簇的下拉实体在 Home Assistant 里归入控制区，但名称固定为「Mode」或「Temperature level」，与风向无关，并且簇的语义是洗碗机、烤箱、温度等。

### 在选项标签里加图标符号（未采纳）

Matter 没有选项图标字段，只能把符号写进标签文字，试用后显示效果不好。

## 影响

- 每个节点有七个端点，Matter 配网后控制器中会出现一个风扇和六个下拉选择。
- 实际风向和多主警告在控制器中是下拉选择，看起来可操作；对只读项的切换会收到错误提示，错误文字由控制器决定。
- 清除多主警告在控制器中是下拉选择，不是按钮，需要多一步操作。
- 往复周期只能在三个值中选择，增加选项需要修改固件。
- 设备端的显示文字固定为英文，没有多语言机制。
- 风向模式是日常会改的设置，在控制器中归入配置类别，不出现在风扇的控制界面。
- 风扇的百分比滑块只能落在 0、33、66、100，其余值被归入相邻档位。
- 各端点的设备类型使用 Mode Select。
