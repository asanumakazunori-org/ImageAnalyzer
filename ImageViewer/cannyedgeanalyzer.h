// cannyedgeanalyzer.h
#pragma once

#include <QObject>
#include <QColor>
#include <QVector>
#include <QPainter>
#include <QTransform>
#include <QSize>

#include <algorithm>   // std::clamp
#include <cstdint>     // int32_t
#include <opencv2/core.hpp>

#include "iaimageanalyzer.h"
#include "EdgeDetector.h"   // ImageProc 側（独自 Canny 実装）

/*
 * CannyEdgeAnalyzer
 *
 * 独自実装 ImageProc::EdgeDetector を使ってエッジ検出を行い、
 * その結果（ポリライン列）を画面上に描画する Analyzer。
 *
 * - execute(): cv::Mat からグレースケール画像を作り、EdgeDetector::detect() を呼ぶ。
 * - draw():   線分としてエッジを描画する（ランダム色 or 勾配強度ヒートマップ）。
 * - hitTestNearestPoint(): 画像座標上でマウスに最も近いエッジ点を選択する。
 *
 * 【新しい Analyzer 実装へのヒント】
 *   - CannyEdgeAnalyzer は「典型的な IaImageAnalyzer の実装例」になっている。
 *   - execute() で cv::Mat を好きな形式に変換し、
 *     ImageProc::xxx などの解析関数を呼ぶスタイルを参考にすると良い。
 *   - draw() では、ImageViewer が設定した transform を利用して、
 *     「画像座標で」線や点を描けばビュー上に重なる。
 */
class CannyEdgeAnalyzer : public IaImageAnalyzer
{
    Q_OBJECT

    // ---- カテゴリ：CannyEdgeDetector ----
    //  PropertyPanel 側では "__category_xxx" というダミー Q_PROPERTY を見て
    //  グループ見出しを自動生成している。
    Q_PROPERTY(QString __category_CannyEdgeDetector MEMBER dummyCategory1)

    Q_PROPERTY(double sigma READ sigma WRITE setSigma NOTIFY parametersChanged)
    Q_PROPERTY(int lowThreshold READ lowThreshold WRITE setLowThreshold NOTIFY parametersChanged)
    Q_PROPERTY(int highThreshold READ highThreshold WRITE setHighThreshold NOTIFY parametersChanged)
    Q_PROPERTY(EdgeColorMode edgeColorMode READ edgeColorMode WRITE setEdgeColorMode NOTIFY parametersChanged)

    // ---- カテゴリ：拡張パラメータ ----
    Q_PROPERTY(QString __category_ExtendedParams MEMBER dummyCategory2)

    Q_PROPERTY(int dirMinDeg READ dirMinDeg WRITE setDirMinDeg NOTIFY parametersChanged)
    Q_PROPERTY(int dirMaxDeg READ dirMaxDeg WRITE setDirMaxDeg NOTIFY parametersChanged)
    Q_PROPERTY(int edgeConnectDist READ edgeConnectDist WRITE setEdgeConnectDist NOTIFY parametersChanged)
    Q_PROPERTY(int edgeMinLength READ edgeMinLength WRITE setEdgeMinLength NOTIFY parametersChanged)

public:
    // エッジの色付け方法
    enum class EdgeColorMode {
        Random,  // ポリラインごとに色を変える
        Heatmap  // 勾配強度に応じて青→赤のヒートマップ
    };
    Q_ENUM(EdgeColorMode)

    explicit CannyEdgeAnalyzer(QObject* parent = nullptr);

    // --- IaImageAnalyzer インタフェース実装 ---

    // メニュー表示用の名前
    QString name() const override { return tr("Canny Edge"); }

    // originalImage : 入力画像（8bit/16bit 1ch または 3ch など）
    //   ここから必要に応じてグレースケール画像を生成し、
    //   ImageProc::EdgeDetector::detect() を呼び出す。
    void execute(const cv::Mat& originalImage) override;

    // imageTransform : 「画像座標 → 画面座標」の変換（ズーム・パンを含む）
    // draw() 内では imageTransform 自体は使っていないが、
    // ImageViewer 側で QPainter に適用済み（p.setWorldTransform）なので、
    // 画像座標で線を書くと、そのまま画面上の正しい位置に描かれる。
    void draw(QPainter& p,
              const QTransform& imageTransform,
              const QSize& imageSize) override;

    // プロパティパネルから直接このオブジェクトを編集してもらう
    QObject* parametersObject() override { return this; }

    // マウスヒットテスト（ImageViewer から呼ばれる）
    int hitTestNearestPoint(const QPointF& imagePoint) override;

    // --- property getter ---
    double sigma() const { return m_sigma; }
    double lowThreshold() const { return m_lowThre; }
    double highThreshold() const { return m_highThre; }
    EdgeColorMode edgeColorMode() const { return m_edgeColorMode; }
    int32_t dirMinDeg() const { return m_dirMinDeg; }
    int32_t dirMaxDeg() const { return m_dirMaxDeg; }
    int32_t edgeConnectDist() const { return m_edgeConnectDist; }
    int32_t edgeMinLength() const { return m_edgeMinLength; }

public slots:
    // PropertyPanel の UI から値が変更されたときに呼ばれる setter 達。
    // setter 内で parametersChanged() を emit することで、
    // MainWindow → executeCurrentAnalyzer() → execute() が再実行される。
    void setSigma(double v);
    void setLowThreshold(int32_t v);
    void setHighThreshold(int32_t v);
    void setEdgeColorMode(EdgeColorMode mode);
    void setDirMinDeg(int32_t v) { m_dirMinDeg = std::clamp(v, -180, 180); emit parametersChanged(); }
    void setDirMaxDeg(int32_t v) { m_dirMaxDeg = std::clamp(v, -180, 180); emit parametersChanged(); }
    void setEdgeConnectDist(int32_t v) { m_edgeConnectDist = std::clamp(v, 0, 10); emit parametersChanged(); }
    void setEdgeMinLength(int32_t v) { m_edgeMinLength = std::clamp(v, 0, 1000); emit parametersChanged(); }

signals:
    // パラメータが変わったことを MainWindow に知らせるためのシグナル
    void parametersChanged();

private:
    // Random モード用：ポリライン数に合わせて色テーブルを用意する
    void ensurePolylineColors();

private:
    // Canny のパラメータ
    double  m_sigma           = 2.0;
    int32_t m_lowThre         = 5;
    int32_t m_highThre        = 10;
    int32_t m_dirMinDeg       = 0;
    int32_t m_dirMaxDeg       = 0;
    int32_t m_edgeConnectDist = 0;
    int32_t m_edgeMinLength   = 10;

    EdgeColorMode m_edgeColorMode = EdgeColorMode::Random;

    // ImageProc::EdgeDetector が返すエッジ列
    ImageProc::Polylines m_edges;

    // 勾配強度の最小・最大値（ヒートマップ描画用）
    double m_minMag = 0.0;
    double m_maxMag = 1.0;

    // Random モードで使う色配列
    QVector<QColor> m_polylineColors;

    // PropertyPanel 用カテゴリダミー文字列
    QString dummyCategory1;
    QString dummyCategory2;

    // マウスで選択されたポリラインおよびその頂点インデックス
    int32_t m_selectedPolyline = -1;
    int32_t m_selectedPoint    = -1;
};
