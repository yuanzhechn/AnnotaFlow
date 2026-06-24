# AnnotaFlow 本地 SAM2 服务

该服务只绑定 `127.0.0.1:8765`，用于同一台电脑上的 Qt C++ 客户端与 Python SAM2 推理进程通信。图片不会上传到网络。

## 1. 创建环境

该服务使用名为 `AnnotaFlow` 的 conda 环境。下面命令中的 `<仓库目录>` 表示 AnnotaFlow 的本地目录。
需要创建环境时：

```powershell
conda env create -f "<仓库目录>\sam2-service\environment.yml"
conda activate AnnotaFlow
```

如果已经创建过环境：

```powershell
conda env update -n AnnotaFlow -f "<仓库目录>\sam2-service\environment.yml"
```

## 2. 准备官方 SAM2 权重

从 [facebookresearch/sam2](https://github.com/facebookresearch/sam2) 下载与配置匹配的 checkpoint，放到 AnnotaFlow 自己的模型目录：

```powershell
<仓库目录>\models\sam2.1_hiera_small.pt
```

默认配置为：

```powershell
$env:ANNOTAFLOW_SAM2_SOURCE="<仓库目录>"
$env:ANNOTAFLOW_SAM2_CHECKPOINT="<仓库目录>\models\sam2.1_hiera_small.pt"
$env:ANNOTAFLOW_SAM2_CONFIG="configs/sam2.1/sam2.1_hiera_s.yaml"
```

服务直接使用官方包的 `build_sam2` 和 `SAM2ImagePredictor`。设备默认自动选择；如果安装的是 CPU 版 PyTorch，就会使用 CPU。

## 3. 启动

通常只需要运行主程序脚本：

```powershell
.\Run-AnnotaFlow.bat
```

服务不会在主程序启动时占用显存。按 `E` 进入 AI 点选模式后，主程序会通过 `start_hidden.py` 隐藏启动服务；正常关闭主程序时服务会一起退出。日志位于：

```text
<仓库目录>\sam2-service\logs\sam2-service.log
```

需要调试服务输出时，再单独启动可见服务窗口：

```powershell
.\sam2-service\Run-SAM2-Service.bat
```

测试 Qt 通信但暂时不加载模型：

```powershell
.\sam2-service\Run-SAM2-Service.bat --mock
```

健康检查：

```powershell
Invoke-RestMethod http://127.0.0.1:8765/health
```

## 接口

`POST /predict`

```json
{
  "image_path": "D:\\dataset\\images\\demo.jpg",
  "point": [512, 384],
  "label": 1
}
```

响应：

```json
{
  "ok": true,
  "bbox": [120, 80, 300, 240],
  "bbox_format": "xywh",
  "score": 0.97
}
```
