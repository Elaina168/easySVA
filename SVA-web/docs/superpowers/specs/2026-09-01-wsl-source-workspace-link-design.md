# WSL 源码工作区与 Gitee 仓库关联设计

日期：2026-09-01

## 背景

Visual Studio 当前打开 WSL Ubuntu 22.04 的 `/opt`。今后的唯一开发根目录确定为 `/opt/SVA`。Windows 目录 `D:\企业应用设计：监控处理` 是此前导入仓库和验收资料所在位置，不再作为长期开发入口。

WSL 已有三个源码仓库：`SVA-backend`、`SVA-server`、`SVA-mediaServer`。它们的提交与 D 盘对应仓库一致，但远程 `origin` 仍指向教师原始仓库。WSL 尚无独立的 `SVA-web` 源码仓库。D 盘 `SVA-web` 包含一个尚未推送的本地提交和四个未提交文件修改，因此迁移完成前不得删除。

## 目标

在 `/opt/SVA` 下建立四个相互独立的源码仓库，保留原有仓库历史和协作边界。每个仓库使用团队 Gitee 仓库作为 `origin`，使用教师原始仓库作为 `upstream`。Visual Studio 和 WSL 命令行都以普通用户 `fanfenmo` 编辑源码，不要求使用 `sudo`。

迁移必须完整保留 D 盘 `SVA-web` 的当前提交、未提交差异和文件内容。现有运行目录、模型、数据库、构建结果和服务进程不属于本次迁移范围，不因源码仓库关联而改变。

## 目录边界

以下目录是源码仓库：

```text
/opt/SVA/SVA-backend
/opt/SVA/SVA-web
/opt/SVA/SVA-server
/opt/SVA/SVA-mediaServer
```

以下目录是运行或数据目录，不创建 Git 仓库，也不改变其用途：

```text
/opt/SVA/backend
/opt/SVA/server
/opt/SVA/mediaServer
/opt/SVA/models
/opt/SVA/backups
/opt/SVA/tmp
```

`/opt/easySVA-installer` 继续作为独立的官方安装脚本仓库，不并入开发源码工作区。

## Git 远程设计

四个源码仓库使用统一的远程命名：

```text
origin   https://gitee.com/easysva-Mo/<仓库名>.git
upstream https://gitee.com/andersonwu/<仓库名>.git
```

其中 `<仓库名>` 分别为 `SVA-backend`、`SVA-web`、`SVA-server` 和 `SVA-mediaServer`。本次只建立和验证远程关系，不自动推送提交，避免在未确认认证状态和目标分支前改变远程仓库。

## 迁移流程

先记录 D 盘四仓库与 WSL 三仓库的提交号、工作区状态和远程地址。随后只调整四个源码目录的所有权，使 `fanfenmo` 能够编辑；运行目录的所有权保持不变。

`SVA-backend`、`SVA-server` 和 `SVA-mediaServer` 保留现有 WSL 工作树，只把 `origin` 改为团队 Gitee 仓库，并补充或校正 `upstream`。`SVA-server` 中已有的 `build` 与 `build-cpu` 属于本机构建产物，保留在本地，不纳入迁移提交。

`SVA-web` 从 D 盘本地仓库克隆到 `/opt/SVA/SVA-web`，使本地领先提交随 Git 历史迁入。然后复制四个已修改文件的当前内容，使 WSL 工作树与 D 盘工作树保持一致，最后设置标准远程地址。

## 失败保护与恢复

迁移全过程不删除 D 盘仓库。若目标目录已存在、提交号不一致、文件复制失败、Git 差异不一致或普通用户仍无写权限，立即停止后续步骤，保留已记录的状态，并修正目标目录而不覆盖 D 盘源文件。

远程连通性验证失败时，保留已经正确设置的本地远程地址并报告认证或网络问题，不执行推送。任何远程推送都需要在本次迁移验证完成后单独进行。

## 验证标准

完成后必须满足以下条件：

1. `/opt/SVA` 下存在四个独立 Git 仓库，目录名与 Gitee 仓库名一致。
2. 四个仓库的 `origin` 指向 `easysva-Mo`，`upstream` 指向 `andersonwu`。
3. WSL `SVA-web` 的 `HEAD` 与 D 盘一致，并保留相同的领先提交。
4. D 盘与 WSL `SVA-web` 的四个修改文件具有相同 SHA-256，`git diff --binary` 表达的变更一致。
5. 三个原有 WSL 源码仓库迁移前后 `HEAD` 不变，没有新增源码差异。
6. `fanfenmo` 能在四个源码目录内创建并删除临时验证文件，运行目录不受影响。
7. 原有服务进程及监听端口状态在迁移前后保持一致。

## D 盘处理原则

本次完成时不删除 D 盘内容。验证报告将明确列出 D 盘哪些仓库已经被 WSL 完整覆盖。只有在确认 WSL 副本完整、远程关系正确且后续开发入口可用后，才单独执行旧副本清理。
