#pragma once

#include <QString>

namespace Em {

// 密码 / 私钥口令的落盘处理。
//
// **现在一律明文保存**：配置文件要能直接备份、导入导出、手动查看，
// DPAPI 绑机器绑账户，换台机器就解不开，不适合这种场景。
//
// 旧版本写下的 "dpapi:..." 值仍然能读：unprotect() 会识别这个前缀并
// 用 DPAPI 解一次；各 Store 在加载时发现这种旧值，会顺手把文件重写成
// 明文（见 ConnectionStore::load / KeyStore::load）。
//
// 明文的代价：任何能读到 %APPDATA%\Em\EmShell\*.json 的程序都能看到密码，
// 这些文件也不要随便发人。界面上有对应提示。
namespace Secret {

// 原样返回明文（保留这个函数是为了以后想换回加密时只改一处）
QString protect(const QString &plain);

// 明文原样返回；旧版 "dpapi:" 密文会尝试用 DPAPI 解开，解不开返回空串
QString unprotect(const QString &value);

// 是否还是旧版的 DPAPI 密文（迁移时用来判断要不要重写文件）
bool looksEncrypted(const QString &value);

} // namespace Secret

} // namespace Em
