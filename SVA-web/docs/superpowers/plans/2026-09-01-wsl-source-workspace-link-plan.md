# WSL 源码工作区关联实施计划

日期：2026-09-01
依据：`docs/superpowers/specs/2026-09-01-wsl-source-workspace-link-design.md`

## 执行顺序

1. 记录 Windows 与 WSL 中各仓库的 `HEAD`、工作树、远程地址、目录所有权以及服务进程和监听端口。
2. 只把 `/opt/SVA/SVA-backend`、`/opt/SVA/SVA-server`、`/opt/SVA/SVA-mediaServer` 的所有权改为 `fanfenmo:fanfenmo`，不改变运行目录。
3. 将三个现有源码仓库的 `origin` 指向 `easysva-Mo`，将 `upstream` 指向 `andersonwu`，保持当前分支和提交不变。
4. 从 D 盘本地 `SVA-web` 克隆完整 Git 历史到 `/opt/SVA/SVA-web`，复制当前四个修改文件，并设置标准远程地址。
5. 比较 D 盘与 WSL `SVA-web` 的 `HEAD`、状态、二进制差异和修改文件 SHA-256；比较其余三个仓库迁移前后的 `HEAD` 与源码差异。
6. 验证四个源码目录可由 `fanfenmo` 写入，验证 Gitee 远程可读取，并复核服务进程和监听端口没有因迁移发生变化。
7. 保留 D 盘全部内容，输出可清理范围和残余风险，不在本次执行中删除旧副本或推送远程。

## 失败处理

任何提交号、文件哈希或工作树差异不一致时立即停止，不删除 D 盘源文件。远程连接失败只记录问题，不推送。权限调整限定在四个源码目录内，不对 `/opt/SVA/backend`、`server`、`mediaServer`、`models`、`backups` 或 `tmp` 执行递归所有权变更。

## 完成信号

完成报告必须列出四个 WSL 源码仓库的路径、`HEAD`、`origin`、`upstream`、工作树状态和普通用户写权限，并给出迁移前后服务进程与端口的对比结果。
