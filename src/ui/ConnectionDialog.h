#pragma once

#include "model/Connection.h"

#include <QDialog>

class QLabel;

namespace Em {

class ConnectionFormPanel;
class ConnectionStore;
class KeyStore;

// 新建 / 编辑连接的公共弹窗。
//
// 全程序只此一处弹连接表单：控制台、所有连接页、终端页空状态都走它。
// 里面装的就是 ConnectionFormPanel（字段、校验、加解密全在那儿），这里只负责
// 套一层对话框，把「取消 / 关掉窗口」接成 reject。
//
// 这里**不落库**：用户提交了就把还没落库的 Connection 写进 *out，存不存、
// 存完要不要连过去，由调用方决定 —— 和 ConnectionFormPanel 的分工一样。
//
// 用静态函数而不是让调用方持有实例：对话框是一次性的，每回开一个干净的，
// 调用方就不必记得先 startNew() 清上回的状态。
class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    // 新建。用户点了主按钮返回 true，结果写进 *out（id 是空的）。
    // submitText 是主按钮上的字：控制台和终端页要的是「保存并连接」。
    static bool createNew(ConnectionStore *store, KeyStore *keys, QWidget *parent,
                          Connection *out, const QString &submitText = QString());

    // 拿一条已有记录**预填**，但仍然算新建（id 会被丢掉）。
    // 控制台上点「最近连接」是这种：参数抄进表单省得手敲，保存时应该新增
    // 一条，而不是把原来那条改掉。
    static bool createFrom(const Connection &seed, ConnectionStore *store, KeyStore *keys,
                           QWidget *parent, Connection *out,
                           const QString &submitText = QString());

    // 编辑已有的一条，提交时带着原来的 id
    static bool edit(const Connection &existing, ConnectionStore *store, KeyStore *keys,
                     QWidget *parent, Connection *out);

protected:
    // 无边框对话框：在头部和空白处按住左键可以拖动窗口。
    // 字段（QLineEdit 等）会自己吃掉鼠标事件，所以在输入框上拖不会挪窗口。
    void mousePressEvent(QMouseEvent *event) override;

private:
    ConnectionDialog(ConnectionStore *store, KeyStore *keys, QWidget *parent);

    // 头部那两行字。原生标题栏没有了（见构造函数），标题得自己画。
    void setHeader(const QString &title, const QString &subtitle);

    ConnectionFormPanel *m_form = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    Connection m_result;
};

} // namespace Em
