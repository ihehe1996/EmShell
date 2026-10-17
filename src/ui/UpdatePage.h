#pragma once

#include <QWidget>

namespace Em {

// 「软件更新」页：版本信息 + 关于信息，点按钮交给 MainWindow 弹 UpdateDialog。
//
// 这一页自己**不发请求**。检查和下载的状态迁移都在对话框里，页面只负责入口 ——
// 这样启动时那次静默检查和手动点按钮走的是同一条路。
// **不要在这里显示更新源**：用户看到的应该是「检查更新」本身，不是背后的仓库。
class UpdatePage : public QWidget
{
    Q_OBJECT

public:
    explicit UpdatePage(QWidget *parent = nullptr);

signals:
    void checkUpdateRequested();
};

} // namespace Em
