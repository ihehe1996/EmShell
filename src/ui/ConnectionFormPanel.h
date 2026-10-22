#pragma once

#include "model/Connection.h"

#include <QMargins>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QTextEdit;

namespace Em {

class ConnectionStore;
class KeyStore;
class PillButton;

// 新建 / 编辑连接的表单。
//
// 从原来的 ConnectionDialog 搬过来的：字段、校验、加解密行为一个没改，
// 只是把「模态对话框」换成了「面板」，好塞进右侧抽屉（见 Drawer）。
// 自己不再 accept/reject —— 校验过了就发 saved()，落库（新增还是更新）
// 由外面决定，控制台那边还得顺手连上去。
class ConnectionFormPanel : public QWidget
{
    Q_OBJECT

public:
    ConnectionFormPanel(ConnectionStore *store, KeyStore *keys, QWidget *parent = nullptr);

    // 清空成新建状态，标题「新建连接」
    void startNew();
    // 拿一条已有记录**预填**，但仍然算新建。
    // 控制台上点「最近连接」就是这种：把参数抄进表单省得手敲，保存时应该
    // 新增一条，而不是把原来那条改掉 —— 所以 id 在这里被丢掉。
    void startNewWith(const Connection &seed);
    // 填上已有的一条，标题「编辑连接」，保存时覆盖原来那条
    void startEdit(const Connection &existing);

    // 主按钮上的字。控制台那边要的是「保存并连接」。
    void setSubmitText(const QString &text);
    QString submitText() const;

    // 走一遍校验并提交，效果等同于用户点面板自己的主按钮：校验通过就发
    // saved()，失败（比如主机地址为空）只弹提示、不发信号。
    // 弹窗的「创建 / 保存」按钮用它，省得再复制一份校验逻辑。
    void submit();

    // 装进弹窗（ConnectionDialog）时关掉：标题由窗口标题栏给，
    // 面板自己那行「新建连接 + 收起按钮」就重了，还多一个没用的 X。
    // 只藏头部和它下面那道渐变，字段、校验、按钮一概不动。
    void setHeaderVisible(bool visible);

    // 同理，弹窗底部用的是自己的「取消 / 创建」：面板那排按钮和它上面那道
    // 渐变一起藏掉。
    void setFooterVisible(bool visible);

    // 表单内容的左右留白。弹窗里交给外层布局统一控制，标题、字段、按钮
    // 才会落在同一条竖线上；不调的话这里自带 22px，弹窗里会多缩一截。
    void setContentMargins(const QMargins &margins);

    // 表单里最后一个可编辑字段（备注）。弹窗拿它把 Tab 顺序接到「取消 / 创建」上。
    QWidget *lastField() const;

    // 整张表单原样铺开需要多高（也就是「中间那块一点不滚」时的高度）。
    // 弹窗靠它把窗口开到刚好装得下：QScrollArea 的 sizeHint 被 Qt 削过一截，
    // 光用 Widget::sizeHint() 定尺寸会矮，最后两个字段要滚才看得见。
    int formContentHeight() const;

signals:
    // 校验通过、字段都读出来了。这个 Connection 还是「未落库」的：
    // 编辑时带着原来的 id，新建时 id 是空的。
    void saved(const Connection &connection);
    void cancelled();

private:
    void buildUi();
    void loadInto(const Connection &connection);
    bool commit();
    void onAuthChanged();
    void refreshKeyList();
    void refreshGroups();

    ConnectionStore *m_store = nullptr;
    KeyStore *m_keys = nullptr;

    // 编辑时带着原来那条的 id / createdAt，commit 只覆盖表单管得着的字段
    Connection m_result;
    bool m_isNew = true;
    QString m_submitOverride;

    QLabel *m_title = nullptr;
    PillButton *m_submit = nullptr;
    QWidget *m_header = nullptr;        // 标题行，装弹窗时要藏
    QWidget *m_headerShadow = nullptr;  // 标题下面那道渐变，跟着一起藏
    QWidget *m_footer = nullptr;        // 底部按钮行，装弹窗时要藏
    QWidget *m_footerShadow = nullptr;  // 按钮上面那道渐变，跟着一起藏
    QWidget *m_formBody = nullptr;      // 滚动区里真正装着表单的那块
    QScrollArea *m_formScroll = nullptr; // 中间那个滚动区

    QLineEdit *m_name = nullptr;
    QComboBox *m_group = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QLineEdit *m_user = nullptr;
    QComboBox *m_auth = nullptr;
    QLineEdit *m_password = nullptr;
    QComboBox *m_key = nullptr;
    QCheckBox *m_savePassword = nullptr;
    QLineEdit *m_tags = nullptr;
    QTextEdit *m_note = nullptr;

    // 「密码 / 密钥」共用一行：标签在认证方式切换时要改字，字段用栈切换
    QLabel *m_secretLabel = nullptr;
    QStackedWidget *m_secretStack = nullptr;
    QLabel *m_keyHint = nullptr;
};

} // namespace Em
