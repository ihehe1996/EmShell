#pragma once

#include "model/Connection.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QScrollArea;
class QShowEvent;
class QVBoxLayout;

namespace Em {

class PillButton;

// 「管理网址」：给一台连接维护一组「名称 + 网址」。
//
// 链接跟着 Connection 一起存进 connections.json，不单独开表；删连接时
// 自然一起没了。对话框里改的是一份副本，点「保存」才交给调用方落库。
//
// 交互：上面两个输入框负责新增；点列表里的一条会把它读回输入框，
// 主按钮变成「更新」；行尾的垃圾桶直接删掉这一条。列表为空时给一行提示。
class LinkDialog : public QDialog
{
    Q_OBJECT

public:
    // 用户点「保存」返回 true，并把结果写进 *out。
    static bool manage(const Connection &connection, QWidget *parent,
                       QVector<ConnectionLink> *out);

protected:
    // 保存时把输入框里还没点「添加」的内容一起收进去，避免白填
    void accept() override;
    // 原生标题栏跟着主题上色。**不能放在构造函数里**：那时还没 show，
    // 提前 winId() 会把主窗口整棵控件树原生化（见 CLAUDE.md 第 15 条）。
    void showEvent(QShowEvent *event) override;

private:
    explicit LinkDialog(const Connection &connection, QWidget *parent);

    void rebuildRows();
    void refreshDraftButtons();
    void beginEdit(int index);
    void clearDraft();
    bool applyDraft();
    void removeAt(int index);

    QLineEdit *m_name = nullptr;
    QLineEdit *m_url = nullptr;
    PillButton *m_apply = nullptr;
    PillButton *m_cancelDraft = nullptr;
    QLabel *m_count = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_rowsHost = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;

    QVector<ConnectionLink> m_links;
    QString m_connectionName;
    int m_editIndex = -1;       // -1 = 新增；否则是在改这一条
    bool m_dwmApplied = false;
};

} // namespace Em
