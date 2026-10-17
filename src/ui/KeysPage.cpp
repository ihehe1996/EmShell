#include "ui/KeysPage.h"

#include "app/Theme.h"
#include "model/KeyStore.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>

namespace Em {

KeysPage::KeysPage(KeyStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    auto *title = makeLabel(QStringLiteral("密钥管理"), "pageTitle", this);
    auto *subtitle = makeLabel(
        QStringLiteral("用密钥登录比密码安全。私钥文件不会离开原位置，这里只登记它的信息。"),
        "pageSubtitle", this);

    auto *importButton = new PillButton(QStringLiteral("导入密钥"), PillButton::Ghost, this);
    importButton->setLeadingIcon(Icon::Upload, 15);
    connect(importButton, &QPushButton::clicked, this, &KeysPage::importKey);

    auto *generateButton = new PillButton(QStringLiteral("生成新密钥"), PillButton::Primary, this);
    generateButton->setLeadingIcon(Icon::Plus, 15);
    connect(generateButton, &QPushButton::clicked, this, &KeysPage::generateKey);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(9);

    auto *titleCol = new QVBoxLayout;
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(2);
    titleCol->addWidget(title);
    titleCol->addWidget(subtitle);

    headerRow->addLayout(titleCol);
    headerRow->addStretch();
    headerRow->addWidget(importButton);
    headerRow->addWidget(generateButton);

    // 列表直接铺在页面底色上，不套 Card —— 右侧内容区只要一层底色
    auto *body = new QWidget(this);
    m_layout = new QVBoxLayout(body);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);

    auto *scrollBody = new QWidget;
    auto *scrollLayout = new QVBoxLayout(scrollBody);
    scrollLayout->setContentsMargins(6, 22, 26, 22);
    scrollLayout->addLayout(headerRow);
    scrollLayout->addSpacing(18);
    scrollLayout->addWidget(body);
    scrollLayout->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(scrollBody);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scroll);

    if (m_store)
        connect(m_store, &KeyStore::keysChanged, this, &KeysPage::rebuild);

    rebuild();
}

void KeysPage::importKey()
{
    if (!m_store)
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择私钥文件"), QString(),
        QStringLiteral("所有文件 (*)"));

    if (path.isEmpty())
        return;

    QString error;
    if (m_store->importFromFile(path, &error))
        emit statusMessage(QStringLiteral("已导入密钥 %1").arg(QFileInfo(path).fileName()));
    else
        QMessageBox::warning(this, QStringLiteral("导入失败"), error);
}

void KeysPage::generateKey()
{
    if (!m_store)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("生成新密钥"));

    auto *nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText(QStringLiteral("例如：id_ed25519_prod"));

    // 「推荐」两个字原来挂在 Ed25519 上。它确实是更好的密钥类型，但内置的
    // libssh2 只支持 RSA，用它就得改走系统 ssh 客户端 —— 两件事都得说清楚，
    // 别让人以为 RSA 才是次一等的选择（见下面的 hint）。
    auto *typeBox = new SelectBox(&dialog);
    typeBox->addItem(QStringLiteral("Ed25519（短且快，推荐）"), QStringLiteral("ed25519"));
    typeBox->addItem(QStringLiteral("RSA 4096（内置 SSH 能直接用它）"), QStringLiteral("rsa"));

    auto *commentEdit = new QLineEdit(&dialog);
    commentEdit->setPlaceholderText(QStringLiteral("备注，会写进公钥末尾，例如 我的笔记本"));

    auto *form = new QFormLayout;
    form->addRow(makeLabel(QStringLiteral("文件名"), "fieldLabel", &dialog), nameEdit);
    form->addRow(makeLabel(QStringLiteral("算法"), "fieldLabel", &dialog), typeBox);
    form->addRow(makeLabel(QStringLiteral("备注"), "fieldLabel", &dialog), commentEdit);

    auto *hint = makeLabel(
        QStringLiteral("生成的文件会放在 %1\n"
                       "Ed25519 更短更快，但内置的 SSH 客户端只支持 RSA；"
                       "用 Ed25519 连接时会自动改用系统自带的 ssh。")
            .arg(KeyStore::keysDirectory()),
        "hint", &dialog);
    hint->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);

    nameEdit->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("还差一点"),
                             QStringLiteral("给密钥起个文件名吧。"));
        return;
    }

    QString error;
    if (m_store->generate(name, typeBox->currentData().toString(),
                          commentEdit->text().trimmed(), &error)) {
        emit statusMessage(QStringLiteral("已生成密钥 %1").arg(name));
    } else {
        QMessageBox::warning(this, QStringLiteral("生成失败"), error);
    }
}

void KeysPage::rebuild()
{
    if (!m_layout)
        return;

    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    if (!m_store || m_store->keys().isEmpty()) {
        auto *empty = makeLabel(
            QStringLiteral("还没有密钥。可以导入已有的私钥，或者让 EmShell 生成一对新的。"),
            "empty");
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        empty->setContentsMargins(0, 30, 0, 30);
        m_layout->addWidget(empty);
        m_layout->addStretch();
        return;
    }

    for (const SshKey &key : m_store->keys()) {
        auto *row = new QFrame;
        row->setProperty("role", QStringLiteral("row"));
        row->setFixedHeight(58);

        auto *icon = new QLabel(row);
        icon->setFixedSize(30, 30);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(Icons::pixmap(Icon::Key,
                                      Theme::color(QStringLiteral("accent-text")), 19));

        auto *name = makeLabel(key.name, "rowTitle", row);
        auto *meta = makeLabel(key.summary().isEmpty() ? key.privateKeyPath : key.summary(),
                               "rowMeta", row);
        meta->setFont(Theme::monoFont(11));

        auto *textCol = new QVBoxLayout;
        textCol->setContentsMargins(0, 0, 0, 0);
        textCol->setSpacing(1);
        textCol->addWidget(name);
        textCol->addWidget(meta);

        auto *copyButton = new PillButton(QStringLiteral("复制公钥"), PillButton::Ghost, row);
        copyButton->setFixedHeight(28);
        auto *deleteButton = new PillButton(QStringLiteral("移除"), PillButton::Danger, row);
        deleteButton->setFixedHeight(28);

        const QString keyId = key.id;
        const QString keyName = key.name;
        const QString publicKey = key.publicKeyText;

        connect(copyButton, &QPushButton::clicked, this, [this, publicKey, keyName] {
            if (publicKey.isEmpty()) {
                QMessageBox::information(
                    this, QStringLiteral("没有公钥"),
                    QStringLiteral("「%1」旁边没有找到 .pub 文件，公钥内容不可用。\n"
                                   "可以在服务器上执行 ssh-keygen -y -f <私钥路径> 导出。")
                        .arg(keyName));
                return;
            }
            QApplication::clipboard()->setText(publicKey);
            emit statusMessage(QStringLiteral("已复制「%1」的公钥").arg(keyName));
        });

        connect(deleteButton, &QPushButton::clicked, this, [this, keyId, keyName] {
            const auto answer = QMessageBox::question(
                this, QStringLiteral("移除密钥"),
                QStringLiteral("确定要把「%1」从列表里移除吗？\n"
                               "磁盘上的密钥文件不会被删除。").arg(keyName));
            if (answer == QMessageBox::Yes) {
                m_store->remove(keyId);
                emit statusMessage(QStringLiteral("已移除「%1」").arg(keyName));
            }
        });

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 0, 10, 0);
        rowLayout->setSpacing(11);
        rowLayout->addWidget(icon);
        rowLayout->addLayout(textCol, 1);
        rowLayout->addWidget(copyButton);
        rowLayout->addWidget(deleteButton);

        m_layout->addWidget(row);
    }

    m_layout->addStretch();
}

} // namespace Em
