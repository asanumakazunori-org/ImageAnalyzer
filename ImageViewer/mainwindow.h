// mainwindow.h
#pragma once

#include <QMainWindow>
#include <opencv2/core.hpp>

class ImageViewer;
class PropertyPanel;
class IaImageAnalyzer;
class CannyEdgeAnalyzer;
class LabelingAnalyzer;
class QAction;
class HistogramWidget;
class QDockWidget;

/*
 * MainWindow
 *
 * アプリケーション全体のメインウィンドウ。
 * - 中央に ImageViewer（画像＋解析オーバーレイ表示）
 * - 右側に PropertyPanel（Analyzer のパラメータ編集）
 * - 右下に HistogramWidget（ヒストグラム表示）
 *
 * 役割:
 *   - 画像の読み込み（OpenCV cv::imread）
 *   - IaImageAnalyzer の生成と切り替え（Canny / Labeling など）
 *   - PropertyPanel と Analyzer の接続（parametersChanged → 再実行）
 *
 * 【新しい Analyzer を追加する大まかな流れ】
 *   1. XxxAnalyzer : public IaImageAnalyzer を実装する。
 *   2. MainWindow に XxxAnalyzer* m_xxxAnalyzer と QAction* m_actionXxx を追加。
 *   3. createMenus() で Analyze メニューにアクションを追加し、
 *      onXxxToggled(bool) スロットを用意して m_currentAnalyzer を切り替える。
 *   4. onXxxToggled(true) の中で
 *         m_currentAnalyzer = m_xxxAnalyzer;
 *         m_viewer->setAnalyzer(m_currentAnalyzer);
 *         m_propertyPanel->setTarget(m_currentAnalyzer->parametersObject());
 *         executeCurrentAnalyzer();
 *      のように呼べば、既存の Canny / Labeling と同じ流れになる。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onOpenImage();                       // File → Load
    void onExit();                            // File → Exit
    void onImageDropped(const QString& path); // Drag & Drop
    void onAnalyzerPropertyChanged();         // PropertyPanel → changed

    // 各 Analyzer の ON/OFF スロット
    void onCannyToggled(bool checked);
    void onLabelToggled(bool checked);

private:
    void createMenus();
    void createDockWidgets();
    void loadImageFromFile(const QString& path);
    void executeCurrentAnalyzer();

    // QImage（24bitカラー）を生成する（画面表示用）
    QImage makeDisplayImage(const cv::Mat& src) const;

private:
    ImageViewer*   m_viewer        = nullptr;
    PropertyPanel* m_propertyPanel = nullptr;

    cv::Mat m_originalMat; // 元画像（解析用）

    CannyEdgeAnalyzer*   m_cannyAnalyzer    = nullptr;
    LabelingAnalyzer*    m_labelingAnalyzer = nullptr;
    IaImageAnalyzer*     m_currentAnalyzer  = nullptr;

    // Analyze メニュー用アクション
    QAction* m_actionCanny = nullptr;
    QAction* m_actionLabel = nullptr;

    HistogramWidget* m_histWidget = nullptr;
    QDockWidget*     m_histDock   = nullptr;

    bool m_isColorImage = false;
};
