#pragma once

#include <QString>

namespace Em {

// 密码之类敏感字段的落盘保护。
//
// Windows 上用 DPAPI（CryptProtectData），密文绑定当前用户账户，
// 别人拿到 connections.json 也解不开。非 Windows 平台上退化为原样返回，
// 并在编译期给一条警告。
//
// 注意：DPAPI 密文是「当前用户 + 当前机器」绑定的。把配置文件拷到
// 另一台机器，密码会解不出来 —— 这时 unprotect() 返回空串，界面应提示
// 用户重新输入，而不是当成空密码去连。
namespace Secret {

// 返回 base64 编码的密文；失败或明文为空时返回空串
QString protect(const QString &plain);

// 传入 protect() 的返回值，还原明文；解不开时返回空串
QString unprotect(const QString &encoded);

// 用来区分「没存过密码」和「存了但解不开」
bool looksEncrypted(const QString &value);

} // namespace Secret

} // namespace Em
