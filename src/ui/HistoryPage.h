#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace Em {

class ConnectionStore;

// 「最近使用」：按时间倒序列出每次连接。
// 样式和「所有连接」一致：固定表头 + 表格行；双击一行可以重新连接。
class HistoryPage : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPage(ConnectionStore *store, QWidget *parent = nullptr);

signals:
    void reconnectRequested(const QString &connectionId);
    void statusMessage(const QString &text);

private:
    void rebuild();

    ConnectionStore *m_store = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QLabel *m_count = nullptr;
    QPushButton *m_clearButton = nullptr;
};

} // namespace Em
