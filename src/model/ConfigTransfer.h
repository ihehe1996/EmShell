#pragma once

#include "model/Connection.h"
#include "model/KeyStore.h"

#include <QString>
#include <QVector>

namespace Em {

class ConnectionStore;

// 配置导入导出。
//
// 导出的是一个自包含的 JSON 文件：连接、分组、历史、密钥索引。
// 密码和私钥口令和解密后的内存值一致，**是明文**（见 model/Secret.h）。
//
// 私钥文件本身不进导出文件 —— 程序一直只保存路径。换机器导入后，
// 需要把私钥文件也拷到同一路径（或重新添加密钥）。
namespace ConfigTransfer {

struct ImportData
{
    QVector<Connection> connections;
    QVector<ConnectionGroup> groups;
    QVector<HistoryEntry> history;
    QVector<SshKey> keys;
};

struct Counts
{
    int connections = 0;
    int groups = 0;
    int history = 0;
    int keys = 0;
};

Counts countsOf(const ImportData &data);

// 写导出文件（QSaveFile，写一半失败不会毁掉已有文件）。
bool writeExportFile(const QString &path, const ConnectionStore &connections,
                     const KeyStore &keys, QString *errorOut = nullptr);

// 读并校验导入文件。格式不对 / 版本太新 / 没有可导入的数据时返回 false。
bool readImportFile(const QString &path, ImportData *out, QString *errorOut = nullptr);

} // namespace ConfigTransfer

} // namespace Em
