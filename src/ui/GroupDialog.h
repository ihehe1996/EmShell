#pragma once

#include "model/Connection.h"

#include <QColor>
#include <QDialog>
#include <QVector>

class QLineEdit;
class QPushButton;

namespace Em {

// 新建 / 编辑分组。**分组表单只有这一个入口**，别再拿 QInputDialog 另起一份 ——
// 颜色这一项 QInputDialog 装不下。
//
// 约定和 ConnectionDialog 一样：对话框**不落库**，只把结果写进 *out，
// 存不存、怎么存由调用方决定（现在是侧栏 SideBar）。
class GroupDialog : public QDialog
{
    Q_OBJECT

public:
    static bool create(QWidget *parent, ConnectionGroup *out);
    static bool edit(const ConnectionGroup &existing, QWidget *parent, ConnectionGroup *out);

private:
    explicit GroupDialog(QWidget *parent);

    void setColor(const QColor &color);
    void refreshSwatches();

    QLineEdit *m_nameEdit = nullptr;
    QPushButton *m_submit = nullptr;
    QVector<QPushButton *> m_swatches;
    QString m_color;
    ConnectionGroup m_result;
};

} // namespace Em
