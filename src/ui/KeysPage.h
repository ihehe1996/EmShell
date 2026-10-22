#pragma once

#include <QWidget>

class QEvent;

class QVBoxLayout;

namespace Em {

class KeyStore;

// 「密钥管理」：导入已有的私钥，或者用系统 ssh-keygen 现场生成一对。
//
// 私钥文件本身不动，还是留在原来的位置，这里只登记路径和公钥信息。
class KeysPage : public QWidget
{
    Q_OBJECT
public:
    explicit KeysPage(KeyStore *store, QWidget *parent = nullptr);

signals:
    void statusMessage(const QString &text);

protected:
    // 钥匙图标颜色是建行时取的，配色变了整页重建一次
    void changeEvent(QEvent *event) override;

private:
    void rebuild();
    void importKey();
    void generateKey();

    KeyStore *m_store = nullptr;
    QVBoxLayout *m_layout = nullptr;
};

} // namespace Em
