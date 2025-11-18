#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <gtest/gtest.h>

#include <opencv2/opencv.hpp>

#include "EdgeDetector.h"
#include "Labeling.h"

using namespace ImageProc;

// エッジ検出の簡単なテスト
TEST(ImageProc_EdgeDetector, DetectOnSimpleImage)
{
    cv::Mat img = cv::Mat::zeros(100, 100, CV_8U);
    // 四角形の輪郭を描く
    cv::rectangle(img, cv::Rect(20, 20, 60, 60), cv::Scalar(255), 2);

    Polylines edges;
    cv::Rect roi(0, 0, img.cols, img.rows);

    int32_t ret = EdgeDetector::detect(
        img,
        edges,
        1.0,    // sigma
        0.1,    // lowThre
        0.3,    // highThre
        roi
    );

    EXPECT_EQ(ret, 0);
    EXPECT_FALSE(edges.empty());
}

// ラベリングの簡単なテスト
TEST(ImageProc_Labeling, RunOnSimpleImage)
{
    cv::Mat img = cv::Mat::zeros(100, 100, CV_8UC3);
    cv::rectangle(img, cv::Rect(10, 10, 30, 30), cv::Scalar(0, 255, 0), cv::FILLED);
    cv::rectangle(img, cv::Rect(50, 50, 30, 30), cv::Scalar(0, 0, 255), cv::FILLED);

    std::vector<LabelRegion> regions;
    Labeling::run(img, /*minArea=*/50, regions);

    EXPECT_GE(static_cast<int>(regions.size()), 1);
}

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8); // コンソールを UTF-8 化

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
