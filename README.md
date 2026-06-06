# AnnotaFlow

AnnotaFlow 是一个使用 **Qt C++** 编写的数据集标注工具。当前是第一阶段版本（0.1.1）：先完成无 AI 的基础标注骨架，把图片浏览、画框、标签输入、保存格式和快捷键流程做顺。

这一阶段没有接入 SAM2、Python 推理服务或其他 AI 模型。

## 当前功能

- 打开图片文件夹
- 图片上一张 / 下一张
- 鼠标拖拽绘制矩形框
- 新框默认沿用上一个标签
- 自动记住当前数据集出现过的所有标签
- 打开数据集后自动加载已有 XML / YOLO 标注并显示在图片上
- 选择保存目录后扫描整个数据集，标签删除提示会统计历史标注
- 已选择保存目录时，新增、修改和删除标注会自动保存
- 初始标签列表为空，不预置示例标签
- 可在数据集标签汇总区新增或删除标签
- 可在当前图片标注区把已有标注改为新标签
- 数据集标签支持自定义颜色
- 支持 `Ctrl+1` 到 `Ctrl+9`、`Ctrl+0` 快速选择类别
- 右侧标签列表可直接编辑
- 点击画布或标签列表选择标注框
- 删除选中的标注
- 撤销上一步修改
- 保存为 Pascal VOC XML
- 保存为 YOLO TXT
- 图片缩放
- 图片平移
- 适应窗口显示

## 快捷键

| 快捷键 | 功能 |
| --- | --- |
| `Ctrl+O` | 打开图片文件夹 |
| `Ctrl+R` | 选择标注保存目录 |
| `A` | 上一张图片 |
| `D` | 下一张图片 |
| `W` | 进入画框模式 |
| `S` | 保存当前图片标注 |
| `Q` | 退出画框模式 / 撤销 |
| `Delete` | 删除选中的标注 |
| `F` | 图片适应窗口 |
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

## 使用流程

1. 打开 AnnotaFlow。
2. 点击 `打开文件夹`，选择图片文件夹。
3. 点击 `保存目录`，选择标注文件保存目录。已有数据集应选择原 XML / YOLO 所在目录的上级输出目录；程序会立即扫描并加载历史标注。
4. 在顶部选择保存格式：`XML` 或 `YOLO`。
5. 按 `W` 进入画框模式。
6. 在图片上拖拽画矩形框。
7. 新数据集初始没有示例标签，请先点击右侧数据集标签区的 `新增标签`。
8. 新标注会自动使用当前选中的类别。
9. 右侧上方是当前数据集出现过的所有标签，单击可切换当前类别。
10. 也可以用 `Ctrl+1` 到 `Ctrl+9`、`Ctrl+0` 快速切换已有类别。
11. 如果某个类别颜色不合适，在右侧数据集标签列表里双击类别选择颜色。
12. 如果某个框需要使用新标签，可选中该框后点击 `编辑 / 新增标签`，或直接双击标签文字修改。
13. 删除数据集标签时会显示该标签对应的标注数量。即使数量为 0，也会要求确认。
14. 如果标签仍有标注，确认删除后会同时删除这些标注，请谨慎操作。
15. 继续拖拽下一个矩形框；画框模式会保持开启，不需要反复按 `W`。
16. 按 `Q` 退出画框模式。
17. 按 `S` 保存当前图片标注。
18. 按 `A` / `D` 切换上一张或下一张图片。

程序会记住每个图片文件夹对应的保存目录和标注格式。第一次打开已有数据集时需要选择一次正确的保存目录，以后重新打开该图片文件夹会自动恢复并加载已有框。

保存后会生成：

```text
输出目录/
+-- xml_labels/
|   +-- image_name.xml
+-- yolo_labels/
    +-- classes.txt
    +-- image_name.txt
```

如果选择 `XML`，主要写入 `xml_labels/`。

如果选择 `YOLO`，主要写入 `yolo_labels/`，并自动维护 `classes.txt`。

AnnotaFlow 还会在输出目录写入一个自己的类别元数据文件：

```text
annotaflow_labels.json
```

它用于保存数据集标签顺序和颜色，不影响标准 XML / YOLO 标注文件。

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
- `app-qt/src/AnnotationIO.cpp`：XML / YOLO 读写
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

## 下一阶段计划

第二阶段可以接入 Python 推理服务：

```text
Qt C++ 桌面端
负责：界面、画布、标注交互、保存文件

Python 推理服务
负责：SAM2 图片分割、视频目标跟踪
```

这样 AnnotaFlow 的主体仍然保持 C++ 的顺滑体验，AI 推理则放在后台服务里运行。
