#pragma once

#include <QWidget>

namespace Em {

// 「软件更新」页：只显示当前版本和更新源，点按钮交给 MainWindow 弹 UpdateDialog。
//
// 这一页自己**不发请求**。检查和下载的状态迁移都在对话框里，页面只负责入口 ——
// 这样启动时那次静默检查和手动点按钮走的是同一条路。
class UpdatePage : public QWidget
{
    Q_OBJECT

public:
    explicit UpdatePage(QWidget *parent = nullptr);

signals:
    void checkUpdateRequested();
};

} // namespace Em
