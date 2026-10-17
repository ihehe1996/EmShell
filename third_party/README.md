# 第三方依赖

## libssh2 1.11.1

原生 SSH 会话（`src/ssh/SshSession.cpp`）用它。

- 官方仓库：https://github.com/libssh2/libssh2
- 来源：`https://codeload.github.com/libssh2/libssh2/tar.gz/refs/tags/libssh2-1.11.1`
- 许可证：BSD-3-Clause（见上游 `COPYING`，本项目未修改其源码）

### 为什么是 WinCNG 后端

WinCNG 是 Windows 自带的加密 API（`bcrypt.dll`）。用它就**不需要 OpenSSL 或
mbedTLS**，依赖面小很多，分发时也不会踩 DLL 版本的坑。

代价是**只支持 RSA**。WinCNG 里没有 ed25519 / ecdsa 的运算能力，libssh2 也就没
编这两条路进去：库里只找得到 `ssh-ed25519`、`ecdsa-sha2-nistp256` 这类**算法名
字符串**（协商表、known_hosts 用），`crypto.c` 那层真正的实现只有 rsa。表现是
这类密钥能一路走到认证，然后在签名那一步失败，报出来的却是「密钥不被服务器
接受」—— 很容易误判成服务器没配好。

所以 `SshSession::supportsKeyType()` 只放 rsa 走原生路径，ed25519 / ecdsa 由
`TerminalPage` 退回系统 ssh 客户端（`ssh -i <私钥>`，那边什么密钥都支持）。
**要让内置路径也支持 ed25519，就得换成 OpenSSL / mbedTLS 后端重建**
（改 `build-libssh2.sh` 里的 `-DCRYPTO_BACKEND`），代价是重新引入那份依赖。

### 产物

```
libssh2/
├── include/          libssh2.h / libssh2_sftp.h / libssh2_publickey.h
└── lib/libssh2.a     静态库（约 340 KB）
```

`CMakeLists.txt` 直接引用这里的头文件和静态库，不走系统安装。
`lib/cmake/` 和 `lib/pkgconfig/` 是 CMake 安装时附带的，本项目用不到，留着无妨。

### 怎么重建

产物已经提交在仓库里，**正常情况下不需要重建**。要换版本或换后端时：

```bash
bash third_party/build-libssh2.sh [工作目录]
```

脚本会下载、解压到独立目录、配置、编译、安装到 `third_party/libssh2/`。
默认工作目录是系统临时目录，也可以指定（比如 `D:/QT/_deps`）以便复用下载缓存。

### 升级版本时注意

1. 改脚本里的 `LIBSSH2_VERSION`
2. 重建后跑一遍 SSH 相关自检：`--sshnative` 和 `--sshgui`
3. 特别留意主机密钥类型的支持情况 —— 见主 README「踩过的坑」里关于
   ed25519 / RSA 的那条
