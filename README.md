# AnnotaFlow

AnnotaFlow 是一个使用 **Qt C++** 编写的数据集标注工具。当前是第一阶段版本：先完成无 AI 的基础标注骨架，把图片浏览、画框、标签输入、保存格式和快捷键流程做顺。

这一阶段没有接入 SAM2、Python 推理服务或其他 AI 模型。

## 当前功能

- 打开图片文件夹
- 图片上一张 / 下一张
- 鼠标拖拽绘制矩形框
- 绘制后输入标签
- 右侧标签列表
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
| `Q` | 取消画框模式 / 撤销 |
| `Delete` | 删除选中的标注 |
| `F` | 图片适应窗口 |
| 鼠标滚轮 | 缩放图片 |
| 鼠标中键拖动 | 平移图片 |
| `Ctrl + 鼠标左键拖动` | 平移图片 |

## 怎么启动

如果已经编译过，可以直接运行：

```powershell
D:\AnnotaFlow\build-msvc\app-qt\AnnotaFlow.exe
```

更推荐使用项目里的启动脚本：

```powershell
D:\AnnotaFlow\Run-AnnotaFlow.bat
```

这个脚本会自动把 Anaconda Qt 的 DLL 目录加入 `PATH`，避免直接打开 exe 时出现找不到 Qt DLL 的问题。

## 使用流程

1. 打开 AnnotaFlow。
2. 点击 `Open Folder`，选择图片文件夹。
3. 点击 `Output Folder`，选择标注文件保存目录。
4. 在顶部选择保存格式：`XML` 或 `YOLO`。
5. 按 `W` 进入画框模式。
6. 在图片上拖拽画矩形框。
7. 输入标签名并确认。
8. 按 `S` 保存当前图片标注。
9. 按 `A` / `D` 切换上一张或下一张图片。

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

## 项目结构

```text
D:/AnnotaFlow
+-- app-qt/
|   +-- include/
|   +-- src/
|   +-- CMakeLists.txt
+-- docs/
+-- samples/
+-- build-msvc/
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

已验证的编译命令：

```powershell
cmd /c "call ""D:\Microsoft Visual Studio\2026\VC\Auxiliary\Build\vcvars64.bat"" && cmake -S D:\AnnotaFlow -B D:\AnnotaFlow\build-msvc -G ""NMake Makefiles"" -DCMAKE_PREFIX_PATH=D:\anaconda2025.06-1\Library"
cmd /c "call ""D:\Microsoft Visual Studio\2026\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build D:\AnnotaFlow\build-msvc"
```

编译成功后，程序位置是：

```text
D:\AnnotaFlow\build-msvc\app-qt\AnnotaFlow.exe
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
