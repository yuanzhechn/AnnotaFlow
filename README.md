# AnnotaFlow

AnnotaFlow 是一个使用 **Qt C++** 编写的数据集标注工具。当前版本为 **0.4.1**，支持主流矩形框目标检测格式、类别管理、整数据集另存为，并接入了本地 SAM2 多点图片辅助标注。

第二阶段的 SAM2 采用“Qt 桌面端 + 本地 Python 推理服务”的结构。服务只绑定 `127.0.0.1`，用于本机进程间通信，不会把图片上传到网络。

## 当前功能

- 打开图片文件夹
- 图片上一张 / 下一张
- 鼠标拖拽绘制矩形框
- SAM2 图片点选：左键添加目标点、右键添加排除点，多点共同生成候选框和绿色 mask 边缘，确认后写入标注
- 新框默认沿用上一个标签
- 自动记住当前数据集出现过的所有标签
- 打开数据集后自动加载已有检测标注并显示在图片上
- 图片目录可以直接选择 `Image/`，也可选择其上级数据集目录让程序定位图片
- 分别选择图片文件夹和标签文件夹
- 当前格式由所选标签文件夹确定；切换标签文件夹可打开同一图片集的不同格式版本
- 已选择标签文件夹时，新增、修改和删除标注会自动保存
- 初始标签列表为空，不预置示例标签
- 可在数据集标签汇总区新增、重命名、合并或删除标签
- 可在当前图片标注区把已有标注改为数据集中的已有标签
- 数据集标签支持自定义颜色
- 支持 `Ctrl+1` 到 `Ctrl+9`、`Ctrl+0` 快速选择类别
- 当前图片标注列表只能选择已有标签，不能在这里新建标签
- 点击画布或标签列表选择标注框
- 删除选中的标注
- 撤销上一步修改
- Pascal VOC XML
- YOLO TXT（支持 `classes.txt` 和常见 `data.yaml`）
- COCO JSON
- LabelMe JSON rectangle
- KITTI TXT 2D detection
- 通用 CSV
- 可将整个数据集另存为其他标注格式，并选择新的标签文件夹
- 图片缩放
- 图片平移
- 适应窗口显示

## 快捷键

| 快捷键 | 功能 |
| --- | --- |
| `Ctrl+O` | 打开图片文件夹 |
| `Ctrl+R` | 选择标签文件夹并确定格式 |
| `Ctrl+Shift+S` | 将全部标注另存为其他格式 |
| `A` | 上一张图片 |
| `D` | 下一张图片 |
| `W` | 进入画框模式 |
| `E` | 进入 AI 点选模式 |
| `R` | 接受 AI 候选框 |
| `Enter` | 接受 AI 候选框 |
| `Esc` | 取消 AI 候选框 |
| `T` | 撤销最后一个 AI 采样点 |
| `Z` | 撤销最近一次标注操作 |
| `Y` | 重做最近撤销的标注操作 |
| `H` | 打开或关闭快捷键总览 |
| `S` | 保存当前图片标注 |
| `Q` | 退出当前模式或取消 AI 候选框 |
| `Delete` | 删除选中的标注 |
| `F` | 图片适应窗口 |
| `Ctrl+=` | 放大图片 |
| `Ctrl+-` | 缩小图片 |
| `Ctrl+1` 到 `Ctrl+9` | 选择右侧数据集标签列表中的第 1 到第 9 个类别 |
| `Ctrl+0` | 选择右侧数据集标签列表中的第 10 个类别 |
| 鼠标滚轮 | 缩放图片 |
| 鼠标中键拖动 | 平移图片 |
| `Ctrl + 鼠标左键拖动` | 平移图片 |

## 怎么启动

如果已经编译过，可以直接运行当前发布版本：

```powershell
D:\AnnotaFlow\bin\AnnotaFlow.exe
```

更推荐使用项目里的启动脚本：

```powershell
D:\AnnotaFlow\Run-AnnotaFlow.bat
```

这个脚本会自动把 Anaconda Qt 的 DLL 目录加入 `PATH`，避免直接打开 exe 时出现找不到 Qt DLL 的问题。
SAM2 服务不会随主程序提前启动。按 `E` 进入 AI 点选模式时，程序才会在后台启动并预热模型；正常关闭 AnnotaFlow 时会同时关闭服务并释放显存。服务日志写入：

```text
D:\AnnotaFlow\sam2-service\logs\sam2-service.log
```

## 使用流程

1. 打开 AnnotaFlow。
2. 点击 `打开文件夹`，选择图片文件夹。
3. 点击 `选择标签文件夹`，选择与图片对应的标签目录，例如同级的 `labels/` 或 `label/`。
4. 程序会自动识别已有格式；空目录或混合格式目录会要求手动确认一次。顶部只显示当前目录的格式。
5. 按 `W` 进入画框模式。
6. 在图片上拖拽画矩形框。
7. 新数据集初始没有示例标签，请先点击右侧数据集标签区的 `新增标签`。
8. 新标注会自动使用当前选中的类别。
9. 右侧上方是当前数据集出现过的所有标签，单击可切换当前类别。
10. 也可以用 `Ctrl+1` 到 `Ctrl+9`、`Ctrl+0` 快速切换已有类别。
11. 如果某个类别颜色不合适，在右侧数据集标签列表里双击类别选择颜色。
12. 如果某个框需要更换标签，可选中该框后点击 `修改为已有标签`，或双击该标注，然后从已有标签中选择。
13. 如果某个框的位置或大小需要重画，直接双击画布中的已有框，再拖出新框替换；按 `Q` 可取消，按 `Z` 可撤销。
14. 删除数据集标签时会显示该标签对应的标注数量。即使数量为 0，也会要求确认。
15. 如果标签仍有标注，确认删除后会同时删除这些标注，请谨慎操作。
16. 继续拖拽下一个矩形框；画框模式会保持开启，不需要反复按 `W`。
17. 按 `Q` 退出画框模式。
18. 按 `S` 保存当前图片标注。
19. 按 `A` / `D` 切换上一张或下一张图片。
20. 需要生成其他格式时点击 `另存为`，选择目标格式和另一个标签文件夹。成功后，新格式和新目录会成为当前保存目标；之后可通过 `选择标签文件夹` 在不同版本之间切换。

## SAM2 点选标注

1. 直接启动 AnnotaFlow：

```powershell
D:\AnnotaFlow\Run-AnnotaFlow.bat
```

按 `E` 进入 AI 点选模式后，主程序会按需拉起本地 SAM2 服务。当前机器上已经创建了 `AnnotaFlow` conda 环境；如果该环境以后被删除，脚本会回退使用已有的 `LabelQuick_env`。默认使用：

```text
D:\LabelQuick\sampro\checkpoints\sam2.1_hiera_small.pt
configs/sam2.1/sam2.1_hiera_s.yaml
```

2. 选择图片文件夹和标签文件夹。
3. 在右侧数据集标签中选择当前类别。
4. 按 `E` 进入 AI 点选模式，鼠标保持普通箭头。
5. 在目标内部左键点一下，Qt 会把当前图片路径和采样点发给本地 Python 服务。
6. 服务返回 mask 和 bbox 后，画布上会显示绿色边缘和候选框。
7. 如果候选范围不够准，可以继续左键添加目标点，或右键添加排除点；所有采样点会共同修正同一个候选框。
8. 按 `R` 接受候选框，标注会使用当前类别并自动保存；按 `Q` 或 `Esc` 取消并清空采样点。

如果暂时不想加载真实模型，可以单独用 mock 服务验证 Qt 交互：

```powershell
D:\AnnotaFlow\sam2-service\Run-SAM2-Service.bat --mock
```

## 数据增强

建议先完成整个数据集的标注和检查，再点击顶部工具栏的 `数据增强`。
增强窗口会再次提示确认，生成过程不会修改原始图片或原始标签。

可多选并叠加：

- 几何变换：水平/垂直翻转、旋转、平移、缩放、随机/中心裁剪、仿射剪切、透视变换
- 颜色增强：亮度、对比度、饱和度、色相、灰度化、Gamma
- 噪声与模糊：高斯噪声、椒盐噪声、高斯模糊、运动模糊、JPEG 压缩
- 遮挡增强：Cutout、Random Erasing、GridMask、Hide-and-Seek
- 混合增强：MixUp、CutMix、Mosaic、Copy-Paste

用户通过“新增增强方案”逐次配置。方案 1、方案 2 可以选择完全不同的增强方法、
概率和参数范围；每个方案会应用到全部原图。双击方案可重新编辑，也可以删除后重建。
几何与混合增强会同步更新目标检测框；完全移出画面或过小的框会被移除。

默认输出结构：

```text
augmented/
├─ images/
├─ labels/
├─ augmentation_report.html
└─ augmentation_manifest.json
```

文件名采用“原图名 + 简短增强链”，不再添加固定流水号。例如：

```text
car_001__hflip_rot-8_rcrop85.jpg
```

其中 `hflip` 表示水平翻转，`rot-8` 表示旋转约 -8°，`rcrop85`
表示随机裁剪并保留约 85% 区域。只有生成结果发生同名时才追加 `_2`、`_3`。
中文原图名会被保留。

其他常见缩写：

- `scale101`：缩放到约 101%
- `shear-6`：仿射剪切约 -6°
- `bri85`：亮度系数约 85%
- `con120`：对比度系数约 120%
- `persp5`：透视扰动约 5%

文件名不会再从某个增强参数中间截断。增强链极长时，只在完整增强项之间缩短，
并以 `plusN` 表示还有 N 项；完整参数始终保存在 `augmentation_manifest.json`。

标签使用当前数据集格式保存，并与增强图片保持同名。`augmentation_manifest.json`
记录原图、随机种子、增强链、混合来源和生成文件，方便复现与追踪。

日常查看建议直接双击 `augmentation_report.html`，它提供中文方案汇总和逐文件明细。
JSON 主要用于程序复现和排查。

要改用其他权重或专门的 `AnnotaFlow` conda 环境，可参考 `D:\AnnotaFlow\sam2-service\README.md` 设置 `ANNOTAFLOW_SAM2_CHECKPOINT`、`ANNOTAFLOW_SAM2_CONFIG` 和 `ANNOTAFLOW_SAM2_SOURCE`。

程序会记住每个图片文件夹对应的标签文件夹和标签格式，以后重新打开该图片文件夹会自动恢复。

推荐的数据集结构：

```text
dataset/
+-- images/
|   +-- image_name.jpg
+-- labels/
    +-- classes.txt
    +-- image_name.txt
    +-- annotaflow_labels.json
```

选择 `labels/` 后，标注文件会直接写入该目录，不再自动创建 `xml_labels/`、`yolo_labels/` 等子目录。旧版本产生的格式子目录仍可读取。

AnnotaFlow 会在当前标签目录写入类别元数据：

```text
annotaflow_labels.json
```

它用于保存标签顺序、颜色和快捷键。`classes.txt` 用于保持导出类别顺序和 ID 稳定。

## 格式兼容范围

| 格式 | 读取 | 保存 | 说明 |
| --- | --- | --- | --- |
| Pascal VOC XML | 支持 | 支持 | 每图一个 XML，读取常见 `Annotations/` 布局 |
| YOLO TXT | 支持 | 支持 | 5 列矩形框；支持 `classes.txt`、`data.yaml` 和常见 train/val/test 布局 |
| COCO JSON | 支持 | 支持 | 标签目录内使用 `instances.json` |
| LabelMe JSON | 支持 | 支持 | 支持两点或四点表示的 `rectangle` shape |
| KITTI TXT | 支持 | 支持 | 当前使用 2D bbox 字段 |
| CSV | 支持 | 支持 | 列为 filename、尺寸、label、xmin/ymin/xmax/ymax |

当前统一内部模型是水平矩形框，因此不会把分割多边形、姿态关键点或旋转框伪装成普通检测框；这些类型将在对应标注能力接入后单独支持。

## 项目结构

```text
D:/AnnotaFlow
+-- app-qt/
|   +-- include/
|   +-- src/
|   +-- CMakeLists.txt
+-- docs/
+-- samples/
+-- build-msvc-release/
+-- Run-AnnotaFlow.bat
+-- CMakeLists.txt
+-- README.md
```

关键文件：

- `app-qt/src/MainWindow.cpp`：主窗口、菜单、工具栏、图片切换、快捷键、保存流程
- `app-qt/src/AnnotationCanvas.cpp`：画布、画框、选择、缩放、平移
- `app-qt/src/AnnotationIO.cpp`：检测格式注册、读写和整数据集导出
- `app-qt/src/ImageLoader.cpp`：图片加载，后续可接 OpenCV

## 编译方式

当前机器上的 Qt 来自 Anaconda，类型是 `win32-msvc`，所以需要使用 MSVC 编译，不能用 MinGW 编译这套 Qt。

如果后续涉及 Python、AI 推理服务或其他依赖环境，统一创建 conda 环境，环境名固定为：

```powershell
conda create -n AnnotaFlow python=3.10
conda activate AnnotaFlow
```

已验证的编译命令：

```powershell
cmd /c "call ""D:\Microsoft Visual Studio\2026\VC\Auxiliary\Build\vcvars64.bat"" && cmake -S D:\AnnotaFlow -B D:\AnnotaFlow\build-msvc-release -G ""NMake Makefiles"" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:\anaconda2025.06-1\Library"
cmd /c "call ""D:\Microsoft Visual Studio\2026\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build D:\AnnotaFlow\build-msvc-release"
```

编译成功后，程序位置是：

```text
D:\AnnotaFlow\build-msvc-release\app-qt\AnnotaFlow.exe
```

对外启动版本会复制到：

```text
D:\AnnotaFlow\bin\AnnotaFlow.exe
```

## OpenCV 说明

这个项目第一阶段目标是先完成标注手感，所以即使当前没有找到 OpenCV C++ 开发包，也可以使用 Qt 的图片加载方式运行。

工程里已经预留了 OpenCV：

```cmake
find_package(OpenCV QUIET COMPONENTS core imgcodecs imgproc)
```

以后安装好 OpenCV C++ 包后，CMake 能找到它时会自动启用 OpenCV 图片解码。

## 当前架构

```text
Qt C++ 桌面端
负责：界面、画布、点选坐标、候选框确认、保存文件

Python 本地推理服务
负责：SAM2 图片编码、点提示推理、mask 转 bbox
```

这样 AnnotaFlow 的主体仍然保持 C++ 的顺滑体验，AI 推理放在后台 Python 进程里运行。下一步可以把服务已经得到的 mask 轮廓返回给 Qt，再扩展负点、多点修正和语义分割标注。
