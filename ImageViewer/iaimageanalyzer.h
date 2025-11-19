// iaimageanalyzer.h
#pragma once

#include <QObject>
#include <QString>
#include <QPainter>
#include <QTransform>
#include <QSize>

#include <opencv2/core.hpp>

/*
 * すべての画像解析クラス（CannyEdgeAnalyzer, LabelingAnalyzer など）の共通インターフェース。
 *
 * このクラスを継承して「○○Analyzer」を作ることで、
 * - MainWindow から execute() を呼んで OpenCV(cv::Mat) で解析を行い、
 * - ImageViewer から draw() を呼んで QPainter でオーバーレイ描画を行う、
 * という共通の流れを実現している。
 *
 * 【新しい Analyzer を追加する手順（概要）】
 *   1. class XxxAnalyzer : public IaImageAnalyzer を定義する。
 *   2. execute(const cv::Mat& originalImage) で、元画像から解析結果を計算して
 *      メンバ変数に保持する。
 *   3. draw(QPainter& painter, const QTransform& imageTransform, const QSize& imageSize) で、
 *      execute() で計算した結果を画像上にオーバーレイ描画する。
 *      （ImageViewer 側で imageTransform が設定されているので、通常は
 *        painter に対してそのまま描画すれば画像座標系に重なる）
 *   4. プロパティを編集したい場合は、Q_PROPERTY マクロを使って
 *      public/protected のメンバに getter/setter を用意する。
 *      PropertyPanel が Qt のメタ情報を使って自動的に UI を構築してくれる。
 *   5. parametersObject() では通常 this を返す。
 *      MainWindow::onAnalyzerPropertyChanged() で parametersChanged シグナルを受け取り、
 *      execute() を再実行する仕組みになっている。
 */
class IaImageAnalyzer : public QObject
{
    Q_OBJECT
public:
    explicit IaImageAnalyzer(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~IaImageAnalyzer() override = default;

    // メニューや PropertyPanel のタイトルなどに表示する解析名
    virtual QString name() const = 0;

    // 解析の実行
    // originalImage : 入力画像（8/16bit、1ch/3ch など OpenCV の cv::Mat）
    //   この関数は「解析結果を内部メンバに保存するだけ」で、
    //   画面への描画は draw() が担当する。
    virtual void execute(const cv::Mat& originalImage) = 0;

    // 解析結果の描画
    //  imageTransform : 画像座標 → ウィジェット座標の変換（ズーム・パンを含む）
    //  imageSize      : 画像サイズ（必要なら使用する。使わない Analyzer もある）
    virtual void draw(QPainter& painter,
                      const QTransform& imageTransform,
                      const QSize& imageSize) = 0;

    // PropertyPanel に渡す QObject（通常は this を返す）。
    // Q_PROPERTY で定義したプロパティが、PropertyPanel 上のウィジェットにマッピングされる。
    virtual QObject* parametersObject() = 0;

    // マウスヒットテスト（デフォルト実装は「ヒットなし」）。
    // ImageViewer::mouseMoveEvent から呼ばれ、
    //   imagePoint : 画像座標系でのマウス位置（ピクセル中心）
    // を渡す。戻り値は「ヒットしたオブジェクトの ID」などに利用する想定。
    // 使わない Analyzer では -1 を返せばよい。
    virtual int hitTestNearestPoint(const QPointF& imagePoint) { Q_UNUSED(imagePoint); return -1; }
};
