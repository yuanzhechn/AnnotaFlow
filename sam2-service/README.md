# AnnotaFlow 本地 SAM2 服务

该服务只绑定 `127.0.0.1:8765`，用于同一台电脑上的 Qt C++ 客户端与 Python SAM2 推理进程通信。图片不会上传到网络。

## 1. 创建环境

当前机器已经创建了专用的 `AnnotaFlow` conda 环境。启动脚本会优先使用它；如果该环境以后被删除，会回退使用已有的 `LabelQuick_env`。需要重建专用环境时：

```powershell
conda env create -f D:\AnnotaFlow\sam2-service\environment.yml
conda activate AnnotaFlow
```

如果已经创建过环境：

```powershell
conda env update -n AnnotaFlow -f D:\AnnotaFlow\sam2-service\environment.yml
```

## 2. 准备官方 SAM2 权重

从 [facebookresearch/sam2](https://github.com/facebookresearch/sam2) 下载与配置匹配的 checkpoint，然后设置：

```powershell
$env:ANNOTAFLOW_SAM2_CHECKPOINT="D:\models\sam2.1_hiera_small.pt"
$env:ANNOTAFLOW_SAM2_CONFIG="configs/sam2.1/sam2.1_hiera_s.yaml"
$env:ANNOTAFLOW_SAM2_DEVICE="cuda"
```

如果使用当前机器已有的 LabelQuick 资源，启动脚本会自动设置：

```powershell
$env:ANNOTAFLOW_SAM2_SOURCE="D:\LabelQuick;D:\LabelQuick\sampro"
$env:ANNOTAFLOW_SAM2_CHECKPOINT="D:\LabelQuick\sampro\checkpoints\sam2.1_hiera_small.pt"
$env:ANNOTAFLOW_SAM2_CONFIG="configs/sam2.1/sam2.1_hiera_s.yaml"
```

服务直接使用官方包的 `build_sam2` 和 `SAM2ImagePredictor`，没有复制 LabelQuick 的实现。

## 3. 启动

通常只需要运行主程序脚本，它会自动启动本服务：

```powershell
D:\AnnotaFlow\Run-AnnotaFlow.bat
```

主程序脚本会通过 `start_hidden.py` 隐藏启动服务，日志位于：

```text
D:\AnnotaFlow\sam2-service\logs\sam2-service.log
```

需要调试服务输出时，再单独启动可见服务窗口：

```powershell
D:\AnnotaFlow\sam2-service\Run-SAM2-Service.bat
```

测试 Qt 通信但暂时不加载模型：

```powershell
D:\AnnotaFlow\sam2-service\Run-SAM2-Service.bat --mock
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
