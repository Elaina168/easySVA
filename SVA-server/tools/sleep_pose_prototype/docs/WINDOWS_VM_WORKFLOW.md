# Windows 本机与 Ubuntu 虚拟机操作流程

本文约定：

- Windows 本机负责浏览器下载素材、查看结果和日常 Git 管理；
- Ubuntu 24.04 虚拟机负责安装 Python、运行 Pose 推理、评测和导出 ONNX；
- 源代码通过 Gitee 同步；视频、权重和输出结果通过虚拟机共享文件夹传输；
- 不要在两台机器之间复制 `.venv`，虚拟环境必须在 Ubuntu 中重新创建。

## 一、Windows 本机操作

### 1. 第一次获取项目

在 PowerShell 中执行：

```powershell
cd D:\你的开发目录
git clone --branch feature/worktimeSleep --single-branch https://gitee.com/FanFenMo/easy-sva-mo.git
cd easy-sva-mo
git status
```

如果本机已有仓库：

```powershell
cd D:\你的开发目录\easy-sva-mo
git fetch origin
git switch feature/worktimeSleep
git pull --ff-only origin feature/worktimeSleep
```

`--ff-only` 能避免无意中产生合并提交。如果提示本地有未提交修改，先运行 `git status`，不要直接覆盖。

### 2. 下载测试素材

从 `SVA-server/tools/sleep_pose_prototype/docs/DATA_MATERIALS.md` 中选择素材，优先下载 1080p。统一命名，例如：

```text
P01_sleep_desk.mp4
P02_sleep_multi_person.mp4
N01_writing.mp4
N02_phone.mp4
```

建议在 Windows 创建一个专用共享目录：

```text
D:\easySVA-share\inputs
D:\easySVA-share\results
```

把下载的视频放入 `D:\easySVA-share\inputs`。原始人物视频不要提交到 Gitee。

### 3. 配置虚拟机共享目录

在 VMware 或 VirtualBox 设置中，把 `D:\easySVA-share` 添加为共享文件夹。

常见虚拟机内路径：

- VMware：`/mnt/hgfs/easySVA-share`
- VirtualBox：`/media/sf_easySVA-share`
- WSL2：Windows 的 `D:\easySVA-share` 对应 `/mnt/d/easySVA-share`

具体路径以虚拟机软件中的共享名称为准。代码优先通过 Gitee 拉取，共享目录主要用于大视频和结果文件。

### 4. 取回结果

算法在虚拟机跑完后，把结果复制到共享目录的 `results`。Windows 中直接打开：

```text
D:\easySVA-share\results\P01\annotated.mp4
D:\easySVA-share\results\P01\frame_features.csv
D:\easySVA-share\results\P01\evaluation.json
```

## 二、Ubuntu 24.04 虚拟机操作

### 1. 安装基础软件和 Python

```bash
sudo apt update
sudo apt install -y git python3 python3-venv python3-pip ffmpeg libgl1

python3 --version
git --version
ffmpeg -version | head -n 1
```

Ubuntu 24.04 的默认 Python 是 3.12，可以直接使用。不要使用 `sudo pip install`，所有 Python 包都装入项目自己的 `.venv`。

### 2. 第一次克隆指定分支

```bash
cd ~/Documents
git clone --branch feature/worktimeSleep --single-branch \
  https://gitee.com/FanFenMo/easy-sva-mo.git
cd easy-sva-mo
git status
```

如果虚拟机中已有仓库：

```bash
cd ~/Documents/easy-sva-mo
git fetch origin
git switch feature/worktimeSleep
git pull --ff-only origin feature/worktimeSleep
```

### 3. 一键创建 Python 环境

```bash
cd ~/Documents/easy-sva-mo/SVA-server/tools/sleep_pose_prototype
bash scripts/setup_ubuntu.sh
```

脚本会安装系统依赖、创建 `.venv`、安装 `requirements.txt` 并运行单元测试。以后重新进入终端只需：

```bash
cd ~/Documents/easy-sva-mo/SVA-server/tools/sleep_pose_prototype
source .venv/bin/activate
```

验证环境：

```bash
python --version
python -c "import cv2, torch, ultralytics; print('environment OK')"
python -m unittest discover -s tests -v
```

### 4. 确认共享目录已经挂载

VMware：

```bash
ls -la /mnt/hgfs/easySVA-share/inputs
```

VirtualBox：

```bash
ls -la /media/sf_easySVA-share/inputs
```

如果能看到 Windows 下载的视频，就说明传输正常。为避免共享目录影响推理速度，建议先把输入复制到虚拟机本地：

```bash
cd ~/Documents/easy-sva-mo/SVA-server/tools/sleep_pose_prototype
mkdir -p materials/raw
cp /mnt/hgfs/easySVA-share/inputs/P01_sleep_desk.mp4 materials/raw/
```

VirtualBox 用户把上面的 `/mnt/hgfs/easySVA-share` 替换为自己的挂载路径。

### 5. 运行第一段视频

```bash
cd ~/Documents/easy-sva-mo/SVA-server/tools/sleep_pose_prototype
source .venv/bin/activate

python run_video.py \
  --source materials/raw/P01_sleep_desk.mp4 \
  --config config/default.yaml \
  --output-dir outputs/P01
```

首次运行会下载 `yolo11n-pose.pt`。结束后检查：

```bash
ls -lh outputs/P01
```

### 6. 把结果传回 Windows

先在虚拟机本地完成推理，再一次性复制结果：

```bash
cp -r outputs/P01 /mnt/hgfs/easySVA-share/results/
```

### 7. 导出 ONNX

完成视频验证和阈值调整后执行：

```bash
python export_onnx.py \
  --model yolo11n-pose.pt \
  --imgsz 640 \
  --opset 17 \
  --output-dir models

ls -lh models
```

应得到 ONNX 文件和 `model_io.json`。随后复制到 Windows：

```bash
cp models/*.onnx models/model_io.json /mnt/hgfs/easySVA-share/results/
```

## 三、每天的最短工作流

Windows 本机：

```powershell
git -C D:\你的开发目录\easy-sva-mo pull --ff-only origin feature/worktimeSleep
```

Ubuntu 虚拟机：

```bash
cd ~/Documents/easy-sva-mo
git pull --ff-only origin feature/worktimeSleep
cd SVA-server/tools/sleep_pose_prototype
source .venv/bin/activate
python run_video.py --source materials/raw/P01_sleep_desk.mp4 --output-dir outputs/P01
```

原则是：代码走 Git，视频和推理结果走共享文件夹，Python 依赖只存在虚拟机的 `.venv` 中。
