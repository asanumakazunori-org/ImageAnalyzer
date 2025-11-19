// labelinganalyzer.h
#pragma once

#include <QObject>
#include <QPainter>
#include <QTransform>
#include <QSize>

#include <opencv2/core.hpp>

#include "iaimageanalyzer.h"
#include "Labeling.h"   // ImageProc 側のラベリング処理

/*
 * LabelingAnalyzer
 *
 * 画像中の領域（ラベル）を抽出し、その外接矩形を描画する Analyzer。
 * ImageProc::Labeling::run() をラッパする形になっている。
 */
class LabelingAnalyzer : public IaImageAnalyzer
{
    Q_OBJECT

    Q_PROPERTY(int minArea READ minArea WRITE setMinArea NOTIFY parametersChanged)

public:
    explicit LabelingAnalyzer(QObject* parent = nullptr);

    QString name() const override { return tr("Labeling"); }

    // originalImage : 入力画像（8/16bit 1ch or 3ch など）
    //   ここから 8bit 3ch BGR を作り、ImageProc::Labeling::run() を呼ぶ。
    void execute(const cv::Mat& originalImage) override;

    // ラベリング結果の矩形を描画する。
    void draw(QPainter& p,
              const QTransform& imageTransform,
              const QSize& imageSize) override;

    QObject* parametersObject() override { return this; }

    int minArea() const { return m_minArea; }

public slots:
    void setMinArea(int v);

signals:
    void parametersChanged();

private:
    int m_minArea = 100;   // 最小面積（画素数）
    std::vector<ImageProc::LabelRegion> m_regions;
};
