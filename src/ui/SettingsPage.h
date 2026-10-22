#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace Em {

class BackgroundPreview;
class ConnectionStore;
class ElidedLabel;
class KeyStore;
class PillButton;

// 「配置管理」：终端、数据和外观设置。
// 用 QSettings 存，路径是注册表 HKCU\Software\Em\EmShell。
//
// 分成「基本设置 / 外观设置 / 导入导出」三个选项卡：基本设置放终端、数据和关于；
// 外观设置放配色和背景图；导入导出负责配置文件的进出。
// 选项卡没做成 QTabWidget —— 那套的页签尺寸 / 内边距在深色主题下很难调得和
// 侧栏一致，这里用可勾选的按钮 + QStackedWidget。
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    SettingsPage(ConnectionStore *store, KeyStore *keys, QWidget *parent = nullptr);

    // 这几项在别处也要用，做成静态的方便直接读
    static QString preferredShell();
    static int terminalFontSize();

    // 背景图。路径可能是空的（没设置 / 文件已经不见了）。
    // MainWindow 启动时和收到 backgroundChanged() 之后都调这几个。
    static QString backgroundImagePath();
    static bool backgroundEnabled();   // 开关。关掉时保留图片和蒙层设置，只是不显示
    static int backgroundDim();        // 0-90，盖在图片上的深色蒙层浓度

signals:
    void statusMessage(const QString &text);
    void terminalFontSizeChanged(int size);

    // 背景图路径或蒙层浓度变了，调用方重新读上面那两个静态函数即可
    void backgroundChanged();

private:
    void openDataFolder();
    void chooseBackground();
    void clearBackground();
    void updateBackgroundUi();

    // 导入导出。replace=true 会先清空现有连接 / 分组 / 历史和密钥索引。
    void exportConfig();
    void importConfig(bool replace);

    ConnectionStore *m_store = nullptr;
    KeyStore *m_keys = nullptr;
    QComboBox *m_paletteBox = nullptr;
    QComboBox *m_shellBox = nullptr;
    QSpinBox *m_fontSizeBox = nullptr;

    QStackedWidget *m_tabStack = nullptr;
    QPushButton *m_basicTab = nullptr;
    QPushButton *m_appearanceTab = nullptr;
    QPushButton *m_transferTab = nullptr;

    QLabel *m_transferSummary = nullptr;

    BackgroundPreview *m_backgroundPreview = nullptr;
    ElidedLabel *m_backgroundFile = nullptr;
    QCheckBox *m_backgroundEnabled = nullptr;
    PillButton *m_clearBackground = nullptr;
    QSlider *m_dimSlider = nullptr;
    QLabel *m_dimValue = nullptr;
};

} // namespace Em
