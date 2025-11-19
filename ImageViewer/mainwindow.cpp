// mainwindow.cpp
#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFile>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

#include "imageviewer.h"
#include "propertypanel.h"
#include "iaimageanalyzer.h"
#include "cannyedgeanalyzer.h"
#include "labelinganalyzer.h"
#include "histogramwidget.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

// -----------------------------------------------
// コンストラクタ
// -----------------------------------------------
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // 中央に画像ビューを配置
    m_viewer = new ImageViewer(this);
    setCentralWidget(m_viewer);

    // プロパティ・ヒストグラム用ドックウィンドウ作成
    createDockWidgets();

    // デフォルトの Analyzer を生成（今は Canny / Labeling の2つ）
    m_cannyAnalyzer    = new CannyEdgeAnalyzer(this);
    m_labelingAnalyzer = new LabelingAnalyzer(this);

    m_currentAnalyzer = nullptr;
    m_viewer->setAnalyzer(nullptr);

    // ImageViewer からのファイルドロップ通知を受け取る
    connect(m_viewer, &ImageViewer::imageDropped,
            this, &MainWindow::onImageDropped);

    // PropertyPanel の propertyChanged → Analyzer を再実行する
    connect(m_propertyPanel, &PropertyPanel::propertyChanged,
            this, &MainWindow::onAnalyzerPropertyChanged);

    // メニュー生成
    createMenus();

    resize(1200, 800);
    setWindowTitle(tr("Image Analyzer"));
}

// -----------------------------------------------
// メニュー生成
// -----------------------------------------------
void MainWindow::createMenus()
{
    // ------------------------------------------------
    // 1. File メニュー（最左）
    // ------------------------------------------------
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

    QAction* openAct = fileMenu->addAction(tr("&Load..."));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenImage);

    QAction* exitAct = fileMenu->addAction(tr("E&xit"));
    connect(exitAct, &QAction::triggered, this, &MainWindow::onExit);

    // ------------------------------------------------
    // 2. Analyze メニュー（解析の切り替え）
    //    新しい Analyzer を追加するときは、ここに QAction を足して
    //    onXxxToggled(bool) のようなスロットをつなぐ。
    // ------------------------------------------------
    QMenu* analyzeMenu = menuBar()->addMenu(tr("&Analyze"));

    m_actionCanny = analyzeMenu->addAction(tr("Edge Detector (&Canny)"));
    m_actionCanny->setCheckable(true);
    connect(m_actionCanny, &QAction::toggled,
            this, &MainWindow::onCannyToggled);

    m_actionLabel = analyzeMenu->addAction(tr("&Labeling"));
    m_actionLabel->setCheckable(true);
    connect(m_actionLabel, &QAction::toggled,
            this, &MainWindow::onLabelToggled);

    // ------------------------------------------------
    // 3. View メニュー（ヒストグラム表示 ON/OFF）
    // ------------------------------------------------
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_histDock->toggleViewAction());
}

// -----------------------------------------------
// Property / Histogram Dock の作成
// -----------------------------------------------
void MainWindow::createDockWidgets()
{
    // --- Properties ---
    QDockWidget* propDock = new QDockWidget(tr("Properties"), this);
    propDock->setObjectName("PropertiesDock");
    propDock->setAllowedAreas(Qt::RightDockWidgetArea);
    // タイトルバーをシンプルにする（空の QWidget に差し替え）
    propDock->setTitleBarWidget(new QWidget());

    m_propertyPanel = new PropertyPanel(propDock);
    propDock->setWidget(m_propertyPanel);
    addDockWidget(Qt::RightDockWidgetArea, propDock);

    // --- Histogram ---
    m_histWidget = new HistogramWidget(this);

    m_histDock = new QDockWidget(tr("Histogram"), this);
    m_histDock->setObjectName("HistogramDock");
    m_histDock->setAllowedAreas(Qt::RightDockWidgetArea);
    m_histDock->setWidget(m_histWidget);
    m_histDock->setTitleBarWidget(new QWidget());
    addDockWidget(Qt::RightDockWidgetArea, m_histDock);

    // Properties の下に Histogram を縦に並べる
    splitDockWidget(propDock, m_histDock, Qt::Vertical);

    // Histogram の高さを「1/4」程度にするための比率 (3:1)
    QList<QDockWidget*> docks  = { propDock, m_histDock };
    QList<int>          sizes  = { 3, 1 };
    resizeDocks(docks, sizes, Qt::Vertical);
}

// -----------------------------------------------
// File → Load
// -----------------------------------------------
void MainWindow::onOpenImage()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Image"),
        QString(),
        tr("Images (*.png *.jpg *.bmp *.tif *.tiff *.jpeg);;All Files (*)")
    );

    if (!path.isEmpty())
        loadImageFromFile(path);
}

// -----------------------------------------------
void MainWindow::onExit()
{
    close();
}

// -----------------------------------------------
// ImageViewer からドロップされたファイルを読み込む
// -----------------------------------------------
void MainWindow::onImageDropped(const QString& path)
{
    loadImageFromFile(path);
}

// -----------------------------------------------
// 元画像を読み込み（OpenCV）
// -----------------------------------------------
void MainWindow::loadImageFromFile(const QString& path)
{
    std::string filename = QFile::encodeName(path).constData();
    cv::Mat mat = cv::imread(filename, cv::IMREAD_UNCHANGED);

    if (mat.empty()) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Failed to load image:\n%1").arg(path));
        return;
    }

    // 解析用の元画像（cv::Mat）を保持
    m_originalMat = mat.clone();

    // モノクロ/カラー判定（ロード時点）
    m_isColorImage = (mat.channels() == 3);

    // 画面表示用の QImage を作成して ImageViewer に渡す
    QImage disp = makeDisplayImage(m_originalMat);
    m_viewer->setImage(disp);

    // ヒストグラム更新
    if (m_histWidget)
        m_histWidget->setImage(disp, m_isColorImage);

    statusBar()->showMessage(tr("Loaded: %1").arg(path), 3000);

    executeCurrentAnalyzer();
}

// -----------------------------------------------
// OpenCV cv::Mat → QImage（24bit BGR）
//   - 表示用のみに使う。解析はあくまで m_originalMat(cv::Mat) を渡す。
// -----------------------------------------------
QImage MainWindow::makeDisplayImage(const cv::Mat& src) const
{
    if (src.empty()) return QImage();

    cv::Mat bgr8;

    if (src.type() == CV_8UC3) {
        bgr8 = src;
    }
    else if (src.type() == CV_8UC1) {
        cv::cvtColor(src, bgr8, cv::COLOR_GRAY2BGR);
    }
    else if (src.type() == CV_16UC1) {
        double minV = 0, maxV = 0;
        cv::minMaxLoc(src, &minV, &maxV);
        double scale = (maxV > minV) ? 255.0 / (maxV - minV) : 1.0;
        double shift = -minV * scale;

        cv::Mat norm8;
        src.convertTo(norm8, CV_8U, scale, shift);
        cv::cvtColor(norm8, bgr8, cv::COLOR_GRAY2BGR);
    }
    else {
        cv::Mat tmp;
        src.convertTo(tmp, CV_8U);
        if (tmp.channels() == 1)
            cv::cvtColor(tmp, bgr8, cv::COLOR_GRAY2BGR);
        else if (tmp.channels() == 3)
            bgr8 = tmp;
        else
            cv::cvtColor(tmp, bgr8, cv::COLOR_GRAY2BGR);
    }

    return QImage(
        bgr8.data,
        bgr8.cols,
        bgr8.rows,
        static_cast<int>(bgr8.step),
        QImage::Format_BGR888
    ).copy();  // データ所有権のため copy() する
}

// -----------------------------------------------
// 現在選択されている Analyzer を実行
// -----------------------------------------------
void MainWindow::executeCurrentAnalyzer()
{
    if (!m_currentAnalyzer) return;
    if (m_originalMat.empty()) return;

    m_currentAnalyzer->execute(m_originalMat);
    m_viewer->update();
}

// ------------------------------------------------------
//  Canny Analyzer 用 toggled スロット
// ------------------------------------------------------
void MainWindow::onCannyToggled(bool checked)
{
    if (checked) {
        // Canny が ON → Labeling を OFF
        m_actionLabel->blockSignals(true);
        m_actionLabel->setChecked(false);
        m_actionLabel->blockSignals(false);

        m_currentAnalyzer = m_cannyAnalyzer;
        m_viewer->setAnalyzer(m_currentAnalyzer);
        m_propertyPanel->setTarget(m_currentAnalyzer->parametersObject());
        executeCurrentAnalyzer();
    }
    else {
        // Canny が OFF → 「解析なし」
        m_currentAnalyzer = nullptr;
        m_viewer->clearAnalyzer();
        m_propertyPanel->setTarget(nullptr);
        m_viewer->update();
    }
}

// ------------------------------------------------------
//  Labeling Analyzer 用 toggled スロット
// ------------------------------------------------------
void MainWindow::onLabelToggled(bool checked)
{
    if (checked) {
        // Labeling が ON → Canny を OFF
        m_actionCanny->blockSignals(true);
        m_actionCanny->setChecked(false);
        m_actionCanny->blockSignals(false);

        m_currentAnalyzer = m_labelingAnalyzer;
        m_viewer->setAnalyzer(m_currentAnalyzer);
        m_propertyPanel->setTarget(m_currentAnalyzer->parametersObject());
        executeCurrentAnalyzer();
    }
    else {
        // Labeling が OFF → 「解析なし」
        m_currentAnalyzer = nullptr;
        m_viewer->clearAnalyzer();
        m_propertyPanel->setTarget(nullptr);
        m_viewer->update();
    }
}

// -----------------------------------------------
// PropertyPanel から「どれかのプロパティが変更された」通知を受けたとき
// -----------------------------------------------
void MainWindow::onAnalyzerPropertyChanged()
{
    executeCurrentAnalyzer();
}
