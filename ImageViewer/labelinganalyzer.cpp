// labelinganalyzer.cpp
#include "labelinganalyzer.h"

#include <opencv2/imgproc.hpp>

using namespace ImageProc;

LabelingAnalyzer::LabelingAnalyzer(QObject* parent)
    : IaImageAnalyzer(parent)
{
}

// Labeling 実行本体
void LabelingAnalyzer::execute(const cv::Mat& originalImage)
{
    m_regions.clear();

    if (originalImage.empty())
        return;

    cv::Mat bgr8;

    // originalImage から「8bit 3ch BGR」を作る
    if (originalImage.channels() == 1) {
        // グレー → 8bit → BGR
        cv::Mat tmp;
        if (originalImage.depth() != CV_8U) {
            originalImage.convertTo(tmp, CV_8U, 1.0 / 256.0);
        } else {
            tmp = originalImage;
        }
        cv::cvtColor(tmp, bgr8, cv::COLOR_GRAY2BGR);
    }
    else if (originalImage.channels() == 3) {
        if (originalImage.depth() == CV_8U) {
            bgr8 = originalImage;
        } else {
            originalImage.convertTo(bgr8, CV_8U, 1.0 / 256.0);
        }
    }
    else if (originalImage.channels() == 4) {
        cv::Mat tmp;
        cv::cvtColor(originalImage, tmp, cv::COLOR_BGRA2BGR);
        if (tmp.depth() == CV_8U) {
            bgr8 = tmp;
        } else {
            tmp.convertTo(bgr8, CV_8U, 1.0 / 256.0);
        }
    }
    else {
        // 想定外のフォーマットのときは、とりあえず 8bit BGR に落とす
        cv::Mat tmp = originalImage;
        if (tmp.channels() == 1) {
            if (tmp.depth() != CV_8U)
                tmp.convertTo(tmp, CV_8U, 1.0 / 256.0);
            cv::cvtColor(tmp, bgr8, cv::COLOR_GRAY2BGR);
        } else {
            if (tmp.depth() != CV_8U)
                tmp.convertTo(tmp, CV_8U, 1.0 / 256.0);
            cv::cvtColor(tmp, bgr8, cv::COLOR_BGRA2BGR);
        }
    }

    // ImageProc::Labeling 実行
    if (!bgr8.empty()) {
        Labeling::run(bgr8, m_minArea, m_regions);
    }
}

// ラベリング結果の外接矩形を描画
void LabelingAnalyzer::draw(QPainter& p,
                            const QTransform& /*imageTransform*/,
                            const QSize& /*imageSize*/)
{
    if (m_regions.empty())
        return;

    p.save();

    QPen pen(Qt::yellow);
    pen.setWidthF(0.0);     // ピクセル単位の最細線
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    for (const auto& r : m_regions) {
        // bbox.x, bbox.y はピクセル左上「角」の座標なので、
        // ImageViewer でピクセル中心を (0,0) にしている都合上、
        // 中心座標系では (-0.5, -0.5) 分だけシフトする。
        QRectF rect(r.bbox.x - 0.5,
                    r.bbox.y - 0.5,
                    r.bbox.width,
                    r.bbox.height);
        p.drawRect(rect);
    }

    p.restore();
}

void LabelingAnalyzer::setMinArea(int v)
{
    if (v <= 0) v = 1;
    if (m_minArea == v)
        return;

    m_minArea = v;
    emit parametersChanged();
}
