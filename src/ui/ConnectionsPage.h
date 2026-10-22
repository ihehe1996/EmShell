#pragma once

#include "model/Connection.h"

#include <QWidget>

class QEvent;
class QLabel;
class QLineEdit;

namespace Em {

class ConnectionList;
class ConnectionStore;
class KeyStore;

// 「所有连接」页：不分页浏览 + 搜索框（页头唯一的筛选控件）。
//
// 新建 / 编辑弹公共的 ConnectionDialog，保存之后直接写 store，
// 不用绕到 MainWindow。
class ConnectionsPage : public QWidget
{
    Q_OBJECT

public:
    ConnectionsPage(ConnectionStore *store, KeyStore *keys, QWidget *parent = nullptr);

    // 同一个页面类复用成「收藏夹」：换掉标题，锁死只看收藏
    void configureAsFavorites();

    // 背景图开启时，搜索框的底色按「蒙层浓度」来；percent < 0 表示
    // 没有背景图，清掉内联样式回到主题默认。
    void setGlassOpacity(int percent);

    // 同一个页面类复用成「某个分组的独立页」：标题 = 分组名，只列这一组。
    // 每次点分组都调一次（分组改名 / 改色后 MainWindow 也会重调刷新）。
    void showGroup(const QString &groupId);

    void setFilter(const QString &keyword);

    // 从外面调（Ctrl+N、历史记录页、首页的快捷入口）：切过来并拉出抽屉
    void openNewConnection();
    void openEditConnection(const QString &id);

signals:
    void connectRequested(const Connection &connection);
    void statusMessage(const QString &text);

protected:
    // 分组页标题前那个色点的兜底色来自主题，配色变了要重算
    void changeEvent(QEvent *event) override;

private:
    void applyFilters();
    void refreshGroupHeader();

    ConnectionStore *m_store = nullptr;
    KeyStore *m_keys = nullptr;
    ConnectionList *m_list = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_count = nullptr;         // 标题右边那个「N 台」
    QLabel *m_groupDot = nullptr;      // 分组页标题前的色点，普通页藏着
    QLineEdit *m_search = nullptr;
    QString m_fixedGroupId;            // 非空 = 本页锁在这个分组上
    QString m_keyword;
    bool m_favoritesOnly = false;      // 收藏夹页在 configureAsFavorites() 里置 true
};

} // namespace Em
