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
        /**
         * @brief ラベリングの実行
         * 
         * @param [in] bgrImage     8bit 3ch BGR 画像
         * @param [in] minArea      最小面積（これより小さい領域は無視）
         * @param [out] outRegions  ラベリング結果（領域ごとのBBox等）
         */
        static void run(
            const cv::Mat& bgrImage,
            const int32_t minArea,
            std::vector<LabelRegion>& outRegions
        );
    };
}
