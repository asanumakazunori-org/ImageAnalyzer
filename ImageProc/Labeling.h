#pragma once

#include <opencv2/opencv.hpp>
#include <vector>

namespace ImageProc
{
    // ラベリングの結果を表す領域情報
    struct LabelRegion
    {
        int label = 0;
        cv::Rect bbox;  // バウンディングボックス
        int area = 0;
    };

    class Labeling
    {
    public:
        // bgrImage : 8bit 3ch BGR 画像
        // minArea  : 最小面積（これより小さい領域は無視）
        // outRegions: ラベリング結果（領域ごとのBBox等）
        static void run(
            const cv::Mat& bgrImage,
            int minArea,
            std::vector<LabelRegion>& outRegions
        );
    };
}
