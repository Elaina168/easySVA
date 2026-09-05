# 睡岗检测素材清单与采集方案

更新日期：2026-09-03

## 结论

公开素材适合快速验证程序是否能跑、姿态特征是否合理，但不能替代目标摄像机素材。最终阈值必须使用与部署现场接近的固定机位、距离、桌椅布局和光照视频。

首轮优先下载下面 8 段 Pexels 视频。Pexels 许可允许免费下载、使用和修改且不强制署名，但不得暗示人物为产品背书，也不得以冒犯或负面方式呈现可识别人物。这里只建议用于内部算法测试，不要把原始人物素材放进公开仓库或演示告警页面。

许可页面：https://www.pexels.com/license/

## A 组：睡岗与疑似睡岗

| 编号 | 建议标签 | 素材 | 用途 |
| --- | --- | --- | --- |
| P01 | sleep | https://www.pexels.com/video/a-woman-sleeping-on-her-work-desk-9063074/ | 趴桌、夜间、单人；验证头部接近肩线与低运动量 |
| P02 | sleep | https://www.pexels.com/video/a-woman-is-laying-on-top-of-a-laptop-27430396/ | 趴在笔记本上且有同事；验证多人和遮挡 |
| P03 | sleep | https://www.pexels.com/video/a-tired-woman-sleeping-on-a-table-7882756/ | 桌面睡眠、文具遮挡；验证头靠手臂 |
| P04 | review | https://www.pexels.com/video/a-man-resting-on-his-desk-6615306/ | 疲劳到休息的模糊边界；人工按实际画面分段标注 |

`review` 不是评测脚本接受的最终标签。下载后应逐帧看视频，把明确睡眠段标成 `sleep`，把工作或短暂休息段标成 `normal` / `short_head_down`。

## B 组：高风险误报素材

| 编号 | 建议标签 | 素材 | 用途 |
| --- | --- | --- | --- |
| N01 | normal | https://www.pexels.com/video/focused-office-worker-writing-at-desk-31183723/ | 写字时持续低头，检验是否误报 |
| N02 | normal | https://www.pexels.com/video/woman-using-a-phone-8439809/ | 看手机，头部可能持续向下 |
| N03 | normal | https://www.pexels.com/video/a-woman-reading-a-document-while-sitting-on-a-desk-7660704/ | 阅读文件，检验头部高度阈值 |
| N04 | normal | https://www.pexels.com/video/man-reading-a-document-shaking-his-head-8731303/ | 阅读、手接近头、摇头；检验头靠手臂和运动特征 |

## 使用方式

1. 在每个素材页面选择“Free download”，优先下载 1080p，不必下载 4K。
2. 文件统一改名为 `P01_sleep_desk.mp4`、`N01_writing.mp4` 等，放入 `materials/raw/`；原视频不要提交 Git。
3. 先运行原型生成 `frame_features.csv` 与 `annotated.mp4`，检查人框、头部和两个肩点是否持续可见。
4. 按实际内容制作同名标签文件，例如 `materials/labels/P01_sleep_desk.csv`。
5. 某些公开视频不足默认 15 秒确认窗口，只能验证关键点和单帧特征。临时联调可用 FFmpeg 重复，但正式评测不能把同一小段循环后当作独立样本。

Ubuntu 安装 FFmpeg：

```bash
sudo apt update
sudo apt install -y ffmpeg
```

仅用于状态机冒烟测试的三次循环示例：

```bash
ffmpeg -stream_loop 2 -i materials/raw/P01_sleep_desk.mp4 \
  -c copy materials/derived/P01_sleep_desk_loop3.mp4
```

运行与评测：

```bash
python run_video.py \
  --source materials/raw/P01_sleep_desk.mp4 \
  --output-dir outputs/P01_sleep_desk

python evaluate.py \
  --features outputs/P01_sleep_desk/frame_features.csv \
  --labels materials/labels/P01_sleep_desk.csv \
  --output outputs/P01_sleep_desk/evaluation.json
```

## 必须自采的目标机位素材

每个正式摄像机机位至少拍摄下面 10 段，推荐每段 40～60 秒，总量约 8～10 分钟。尽量使用两名以上参与者并取得授权。

| 编号 | 场景 | 标注重点 |
| --- | --- | --- |
| C01 | 正常坐姿值守 | 全程 normal |
| C02 | 持续写字或填写表格 | 全程 normal，重点看低头误报 |
| C03 | 持续看手机 | 全程 normal，重点看低运动量误报 |
| C04 | 阅读纸质资料 | 全程 normal |
| C05 | 分别低头 2、5、10 秒后抬头 | short_head_down，必须不报警 |
| C06 | 头枕左臂趴睡至少 30 秒 | sleep |
| C07 | 头枕右臂趴睡至少 30 秒 | sleep |
| C08 | 不靠手臂、低头静止至少 30 秒 | sleep，检验低运动量证据 |
| C09 | 人物被显示器/桌面部分遮挡或短暂离开 | 检验 UNKNOWN 与轨迹过期 |
| C10 | 两人同时出现，一人工作一人睡岗 | 按 track_id 标注，检验多人隔离 |

同一动作至少覆盖近、中、远三个位置；若现场存在昼夜差异，再分别采集亮光和暗光版本。最终验证集应由未参与阈值调优的人或机位组成。

## 可作为第二阶段研究参考的数据集

### SCB-Dataset3 / SCB-Dataset5

入口：https://github.com/Whiffe/SCB-dataset

优势：包含阅读、写字、看手机、低头和趴桌等非常接近本任务的课堂画面，适合观察困难负样本和遮挡情况。

限制：仓库明确规定仅限学术研究、个人学习和非商业使用；数据以静态标注帧为主，不能直接评价“持续 15 秒”的时序逻辑。若主项目有商业交付可能，不要把该数据集或其权重打包进产品，除非取得作者书面授权。

## 素材验收规则

- 画面中头部与双肩至少大部分时间可见；
- 人物肩宽建议不少于 40 像素，否则 640 输入下关键点波动会明显增大；
- 原视频、标签、配置和输出目录一一对应；
- 训练集、调参集、最终验证集按人物或机位拆分，不能只随机拆帧；
- 保存来源 URL、下载日期和许可快照；
- 自采素材保存参与者授权和用途范围，不在公开 Git 仓库提交人脸视频。
