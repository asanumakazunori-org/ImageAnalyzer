#include "Labeling.h"

namespace ImageProc
{
    void Labeling::run(
        const cv::Mat& bgrImage,
        const int32_t minArea,
        std::vector<LabelRegion>& outRegions)
    {
        outRegions.clear();
        if (bgrImage.empty())
            return;

        // 簡易的な2値化 → 接続成分解析
        cv::Mat gray, bin;
        cv::cvtColor(bgrImage, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, bin, 0, 255, cv::THRESH_OTSU);

        cv::Mat labels, stats, centroids;
        int nLabels = cv::connectedComponentsWithStats(
            bin, labels, stats, centroids);

        for (int i = 1; i < nLabels; ++i) { // 0番は背景
            int area = stats.at<int>(i, cv::CC_STAT_AREA);
            if (area < minArea)
                continue;

            int x = stats.at<int>(i, cv::CC_STAT_LEFT);
            int y = stats.at<int>(i, cv::CC_STAT_TOP);
            int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
            int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);

            LabelRegion r;
            r.label = i;
            r.area = area;
            r.bbox = cv::Rect(x, y, w, h);
            outRegions.push_back(r);
        }
    }
}
