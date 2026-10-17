#pragma once

#include <QHash>
#include <QWidget>

class QPushButton;
class QVBoxLayout;

namespace Em {

class NavButton;
class ConnectionStore;

// 侧边栏能切到的页面。MainWindow 用它决定右侧显示哪一页。
//
// 原来最前面还有个 Home（控制台总览页），那一页已经整个删掉了，取而代之的是
// 顶上那个「+ 新建连接」按钮 —— 它不切页，只弹新建连接对话框。
enum class AppPage {
    Connections,
    History,
    Favorites,
    Terminal,
    Keys,
    Settings,
    Updates,
};

class SideBar : public QWidget
{
    Q_OBJECT

public:
    explicit SideBar(ConnectionStore *store, QWidget *parent = nullptr);

    void setCurrentPage(AppPage page);
    AppPage currentPage() const { return m_current; }

    // 「分组」区块里某一项被高亮（点分组进它自己的页时由 MainWindow 调）。
    // setCurrentPage() 会自动清掉这个高亮。
    void setCurrentGroup(const QString &groupId);
    QString currentGroupId() const { return m_currentGroup; }

    // 「新建连接」按钮的底色透明度。设了背景图时跟「蒙层浓度」一致；
    // percent < 0 表示没有背景图，恢复主题里的实色。
    void setGlassOpacity(int percent);

signals:
    void pageRequested(AppPage page);

    // 顶上那个「+ 新建连接」被点了。**不切页**，调用方就地弹对话框就行。
    void newConnectionRequested();

    // 左键点了某个分组：打开这个分组的独立页
    void groupRequested(const QString &groupId);
    void statusMessage(const QString &text);

private:
    void refreshCounts();
    void rebuildGroups();        // 结构变化（增删改、排序、改色）
    void refreshGroupCounts();   // 只是连接数变了，不重建
    void showGroupMenu(const QString &groupId, const QPoint &globalPos);
    void createGroup();
    void editGroup(const QString &groupId);
    void deleteGroup(const QString &groupId);

    ConnectionStore *m_store = nullptr;
    QPushButton *m_cta = nullptr;
    QHash<int, NavButton *> m_buttons;      // key = int(AppPage)
    QHash<QString, NavButton *> m_groupButtons;
    QVBoxLayout *m_groupLayout = nullptr;
    QWidget *m_groupBody = nullptr;
    QString m_currentGroup;                 // 非空 = 当前停在某个分组页
    AppPage m_current = AppPage::Connections;
};

} // namespace Em
