#include "EdgeDetector.h"
#include <cassert>
#include <queue>

using namespace ImageProc;

// ROI+1（1pix外側まで拡張）
inline cv::Rect expandRoiBy1(const cv::Rect& roi, const cv::Size& size) {
    int x = std::max(0, roi.x - 1);
    int y = std::max(0, roi.y - 1);
    int ex = std::min(size.width, roi.x + roi.width + 1);
    int ey = std::min(size.height, roi.y + roi.height + 1);
    return cv::Rect(x, y, ex - x, ey - y);
}

// ---------- σ→カーネル長 ----------
inline int kernelSizeFromSigma_OpenCV(double sigma, int min_ksize = 3) {
    int k = cvRound(sigma * 6.0);
    if ((k & 1) == 0) ++k;                 // 奇数化
    return std::max(min_ksize, k);
}

// ---------- 1Dガウス / 1Dガウス一次微分（教科書の式） ----------
inline cv::Mat1d makeGaussian1D(double sigma, int ksize, bool normalize_sum = false) {
    const int r = ksize / 2;
    const double inv_sigma = 1.0 / sigma;
    const double inv_sigma2 = inv_sigma * inv_sigma;
    const double c = 1.0 / (std::sqrt(2.0 * M_PI) * sigma);

    cv::Mat1d k(ksize, 1);
    double sum = 0.0;
    for (int i = 0; i < ksize; ++i) {
        const double x = i - r;
        const double v = c * std::exp(-0.5 * (x * x) * inv_sigma2);
        k(i, 0) = v;
        sum += v;
    }
    if (normalize_sum && sum > 0) k /= sum; // OpenCV流に寄せたい場合
    return k;
}
inline cv::Mat1d makeGaussianDeriv1D(double sigma, int ksize, bool match_gaussian_norm = false) {
    // match_gaussian_norm=false なら「連続式そのまま」（推奨）
    // true にすると、平滑核を sum=1 正規化したとみなして g'(x) の定数スケールを合わせます
    const int r = ksize / 2;
    const double inv_sigma = 1.0 / sigma;
    const double inv_sigma2 = inv_sigma * inv_sigma;
    const double c = 1.0 / (std::sqrt(2.0 * M_PI) * sigma); // g(x) の係数
    const double scale = match_gaussian_norm ? 1.0 : c;     // 正規化しないなら連続式の係数を残す

    cv::Mat1d k(ksize, 1);
    for (int i = 0; i < ksize; ++i) {
        const double x = i - r;
        const double g = std::exp(-0.5 * (x * x) * inv_sigma2);
        // g'(x) = -(x/σ^2) * c * exp(...)
        k(i, 0) = (-x * inv_sigma2) * scale * g;
    }
    // （注）導関数カーネルは“総和0”なので追加の正規化は通常不要
    return k;
}

// 8方向量子化（0..7）。中心角は {0,45,90,135,180,-135,-90,-45} に相当
inline uint8_t quantizeDir8(double theta) {
    // theta: [-π, π] を想定
    double a = theta;
    if (a < 0) a += 2.0 * M_PI;                    // [0, 2π)
    int bin = static_cast<int>(std::floor((a + M_PI / 8.0) / (M_PI / 4.0))) & 7;
    return static_cast<uint8_t>(bin);
}

int32_t EdgeDetector::detect(
    const cv::Mat& src,
    Polylines& outEdges,
    const double sigma,
    const int32_t lowThre,
    const int32_t highThre,
    const cv::Rect& roi,
    const cv::Mat& validMask,
    const double minDir,
    const double maxDir)
{
    CV_Assert(src.channels() == 1);
    CV_Assert(src.depth() == CV_8U || src.depth() == CV_16U);

    const cv::Size sz = src.size();
    const cv::Rect roi_clip = roi & cv::Rect(0, 0, sz.width, sz.height);
    const cv::Rect roi_p1 = expandRoiBy1(roi_clip, sz);

    cv::Mat edge_amp;   // CV_64F, 同サイズ
    cv::Mat edge_dir;   // CV_64F, [-pi, pi]
    cv::Mat edge_code;  // CV_8U,  0..7
    cv::Mat grad_x;     // CV_64F（デバッグ用。不要なら消してOK）
    cv::Mat grad_y;     // CV_64F

    // 1) 入力をCV_64Fへ
    double maxVal = (src.depth() == CV_8U) ? 255.0 : 65535.0;
    cv::Mat src64;
    src.convertTo(src64, CV_64F, 1.0 / maxVal);

    // 2) カーネル作成（OpenCV流の6σ推奨。SciPy流にしたいなら置換）
    const int ksize = kernelSizeFromSigma_OpenCV(sigma);
    const cv::Mat1d kG = makeGaussian1D(sigma, ksize, /*normalize_sum=*/false);   // 連続式そのまま
    const cv::Mat1d kGd = makeGaussianDeriv1D(sigma, ksize, /*match_gaussian_norm=*/false);

    // 3) セパラブル畳み込みで勾配（水平/垂直）
    //    水平勾配 d/dx: 垂直に平滑, 水平に微分
    //    垂直勾配 d/dy: 水平に平滑, 垂直に微分
    cv::sepFilter2D(src64, grad_x, CV_64F, kGd.t(), kG, cv::Point(-1, -1), 0.0, cv::BORDER_REPLICATE);
    cv::sepFilter2D(src64, grad_y, CV_64F, kG.t(), kGd, cv::Point(-1, -1), 0.0, cv::BORDER_REPLICATE);

    // 4) 出力バッファ確保（全体サイズ。とりあえずゼロ埋め）
    edge_amp = cv::Mat::zeros(sz, CV_64F);
    edge_dir = cv::Mat::zeros(sz, CV_64F);
    edge_code = cv::Mat::zeros(sz, CV_8U);

    // 5) roi+1 のみ計算して書き込み
    for (int y = roi_p1.y; y < roi_p1.y + roi_p1.height; ++y) {
        const double* gx = grad_x.ptr<double>(y);
        const double* gy = grad_y.ptr<double>(y);

        double* pa = edge_amp.ptr<double>(y);
        double* pd = edge_dir.ptr<double>(y);
        uint8_t* pc = edge_code.ptr<uint8_t>(y);

        for (int x = roi_p1.x; x < roi_p1.x + roi_p1.width; ++x) {
            const double dx = gx[x], dy = gy[x];
            const double amp = std::hypot(dx, dy);   // sqrt(dx^2 + dy^2)
            const double ang = std::atan2(dy, dx);  // [-pi, pi]

            pa[x] = amp;
            pd[x] = ang;
            pc[x] = quantizeDir8(ang);
        }
    }

    const int32_t width = sz.width;
    const int32_t height = sz.height;
    //cv::Rect roi2(roi_clip.x, roi_clip.y, roi_clip.width, roi_clip.height);

    //エッジピーク（非極値抑制）
    cv::Mat edge_peak = cv::Mat::zeros(height, width, CV_8UC1);
    calcEdgePeakImage(edge_amp.ptr<double>(), edge_code.ptr<uint8_t>(), width, height, width, roi_clip, edge_peak.ptr<uint8_t>());

    //ヒステリシス
    const double low_thre = lowThre / maxVal;
    const double high_thre = highThre / maxVal;
    HysteresisThreshold(edge_peak.ptr<uint8_t>(), edge_amp.ptr<double>(), width, height, width, roi_clip, low_thre, high_thre);

    //細線化    
    calcThinningImage(edge_peak.ptr<uint8_t>(), width, height, width, roi_clip);

    //マスク
    if (!validMask.empty())
    {
        maskingPeakmap(edge_peak.ptr<uint8_t>(), width, height, width, roi_clip, validMask.ptr<uint8_t>());
    }

    //勾配フィルタ
    filterByEdgeDir(edge_peak.ptr<uint8_t>(), edge_dir.ptr<double>(), width, height, width, roi_clip, minDir, maxDir);

    //エッジ追跡
    trackEdgePeak(edge_peak.ptr<uint8_t>(), edge_amp.ptr<double>(), edge_dir.ptr<double>(), edge_code.ptr<uint8_t>(), width, height, width, roi_clip, outEdges);

    return 0;
}


inline cv::Rect normROI(const cv::Mat& img, const cv::Rect& roi) {
    if (roi.width <= 0 || roi.height <= 0) return { 0,0,img.cols,img.rows };
    return (roi & cv::Rect(0, 0, img.cols, img.rows));
}

inline cv::Mat toFloatView(const cv::Mat& src) {
    CV_Assert(src.channels() == 1 && (src.depth() == CV_8U || src.depth() == CV_16U));
    cv::Mat f; src.convertTo(f, CV_32F); // スケールなし（元アルゴリズムの閾値スケール保持）
    return f;
}

// ksize は未指定なら 2*ceil(3σ)+1
inline int kernelSizeFromSigma(double sigma) {
    return std::max(3, 2 * int(std::ceil(3.0 * sigma)) + 1);
}

// 1D ガウス g と 1D ガウス一次微分 dg（厳密式）を作る
inline void makeGaussAndDeriv(double sigma, int ksize,
    cv::Mat1d& g, cv::Mat1d& dg)
{
    if (ksize <= 0) ksize = kernelSizeFromSigma(sigma);
    g = cv::getGaussianKernel(ksize, sigma, CV_64F); // 正規化済み（和=1）

    dg.create(ksize, 1);
    const int c = ksize / 2;
    const double s2 = sigma * sigma;
    for (int i = 0; i < ksize; ++i) {
        const double x = double(i - c);
        // d/dx G(x) = -(x/σ^2) * G(x)   （ご提示の式と等価）
        dg(i) = -(x / s2) * g(i);
    }
    // 和はほぼ 0（理論上 0）。必要なら微小オフセットを打ち消す：
    // dg -= cv::mean(dg)[0];
}

// ROI だけに sepFilter2D を掛けて全体に貼り戻す（境界は REPLICATE）
inline void applySep(const cv::Mat& src, cv::Mat& dst,
    const cv::Mat& kx, const cv::Mat& ky,
    const cv::Rect& R)
{
    dst = cv::Mat::zeros(src.size(), CV_32F);
    cv::Mat srcR32 = toFloatView(src(R));
    cv::Mat outR32;
    cv::sepFilter2D(srcR32, outR32, CV_32F, kx, ky,
        cv::Point(-1, -1), 0.0, cv::BORDER_REPLICATE);
    outR32.copyTo(dst(R));
}

// -------------------- x 方向ガウス微分 --------------------
void EdgeDetector::calcHorizontalDiffImage(
    const cv::Mat& srcGray,
    cv::Mat& gradX,
    const double sigma,
    const cv::Rect& roi)
{
    CV_Assert(srcGray.channels() == 1);
    const cv::Rect R = normROI(srcGray, roi);

    cv::Mat1d g, dg;
    makeGaussAndDeriv(sigma, /*ksize=*/0, g, dg);

    // x: dg,  y: g  （分離畳み込みで厳密な DoG 勾配）
    applySep(srcGray, gradX, dg, g, R);
}

// -------------------- y 方向ガウス微分 --------------------
void EdgeDetector::calcVerticalDiffImage(
    const cv::Mat& srcGray,
    cv::Mat& gradY,
    const double sigma,
    const cv::Rect& roi)
{
    CV_Assert(srcGray.channels() == 1);
    const cv::Rect R = normROI(srcGray, roi);

    cv::Mat1d g, dg;
    makeGaussAndDeriv(sigma, /*ksize=*/0, g, dg);

    // x: g,  y: dg
    applySep(srcGray, gradY, g, dg, R);
}

// -------- 勾配強度・方向・方位コード（4/8方向） --------
void EdgeDetector::calcEdgeAmpDir(
    const cv::Mat& srcGray,
    cv::Mat& edgeAmp,
    cv::Mat& edgeDir,
    cv::Mat& edgeCode,
    const double sigma,
    const cv::Rect& roi,
    const int32_t quantizeDirections)
{
    CV_Assert(srcGray.channels() == 1);
    CV_Assert(quantizeDirections == 4 || quantizeDirections == 8);

    const cv::Rect R = normROI(srcGray, roi);

    // 1) DoG 勾配
    cv::Mat gx, gy;
    calcHorizontalDiffImage(srcGray, gx, sigma, R); // CV_32F, full-size
    calcVerticalDiffImage(srcGray, gy, sigma, R);

    // 2) 強度・方向
    edgeAmp = cv::Mat::zeros(srcGray.size(), CV_32F);
    edgeDir = cv::Mat::zeros(srcGray.size(), CV_32F);
    edgeCode = cv::Mat::zeros(srcGray.size(), CV_8U);

    cv::Mat magR, ang0to2piR;
    cv::magnitude(gx(R), gy(R), magR);               // CV_32F
    cv::phase(gx(R), gy(R), ang0to2piR, false);      // radians [0, 2π)

    // [-π, π) へ
    cv::Mat angR = ang0to2piR - float(CV_PI);
    // wrap は理論的には不要だが念のため
    {
        cv::Mat m1 = (angR < -CV_PI); angR.setTo(angR + 2.f * (float)CV_PI, m1);
        cv::Mat m2 = (angR >= CV_PI); angR.setTo(angR - 2.f * (float)CV_PI, m2);
    }

    magR.copyTo(edgeAmp(R));
    angR.copyTo(edgeDir(R));

    // 3) 方位量子化（NMS 等の方向選択に使用）
    cv::Mat codeR(angR.size(), CV_8U, cv::Scalar(0));
    if (quantizeDirections == 4) {
        // π 周期性を考慮（勾配方向は 180°周期）
        cv::Mat a = cv::abs(angR);                // [0, π)
        cv::Mat deg; a.convertTo(deg, CV_32F, 180.0f / (float)CV_PI);
        codeR = ((deg + 22.5f) / 45.0f);
        codeR &= 3;
    }
    else {
        cv::Mat a = angR + float(CV_PI);          // [0, 2π)
        cv::Mat deg; a.convertTo(deg, CV_32F, 180.0f / (float)CV_PI);
        codeR = ((deg + 11.25f) / 22.5f);
        codeR &= 7;
    }
    codeR.copyTo(edgeCode(R));
}

template <typename T>
void EdgeDetector::calcEdgeAmpDir(
    const T* image,
    const int32_t width, const int32_t height, const int32_t stride_byte,
    const double* smoothFilter, const double* diffFilter, const int32_t fsize,
    const cv::Rect _roi,
    double* edgeAmp, double* edgeDir, unsigned char* edgeCode,
    const int32_t mode)
{
    assert(image != nullptr);
    assert(edgeAmp != nullptr);
    assert(edgeDir != nullptr);
    assert(edgeCode != nullptr);
    assert(smoothFilter != nullptr);
    assert(diffFilter != nullptr);

    cv::Rect roi = _roi & cv::Rect(0,0, width, height);

    int32_t stride_pix = stride_byte / sizeof(T);
    assert(stride_pix == width);

    //微分画像（水平）
    std::unique_ptr< double[] > diff_image_h(new double[stride_pix * height]());
    if (mode == 0 || mode == 1)
    {
        calcHorizontalDiffImage(image, width, height, stride_byte, smoothFilter, diffFilter, fsize, roi, diff_image_h.get());
    }

    //微分画像（垂直）
    std::unique_ptr< double[] > diff_image_v(new double[stride_pix * height]());
    if (mode == 0 || mode == 2)
    {
        calcVerticalDiffImage(image, width, height, stride_byte, smoothFilter, diffFilter, fsize, roi, diff_image_v.get());
    }

    //エッジ強度、方向
    int32_t sx = roi.x;
    int32_t sy = roi.y;
    int32_t ex = roi.x + roi.width - 1;
    int32_t ey = roi.y + roi.height - 1;


#pragma omp parallel for 
    for (int32_t y = sy; y <= ey; ++y)
    {
        double* p_diffH = &diff_image_h[stride_pix * y + sx];
        double* p_diffV = &diff_image_v[stride_pix * y + sx];

        double* p_amp = &edgeAmp[stride_pix * y + sx];
        double* p_dir = &edgeDir[stride_pix * y + sx];
        unsigned char* p_code = &edgeCode[stride_pix * y + sx];

        for (int32_t x = sx; x <= ex; ++x)
        {
            *p_amp = sqrt((*p_diffH) * (*p_diffH) + (*p_diffV) * (*p_diffV));
            *p_dir = atan2((*p_diffV), (*p_diffH));
            *p_code = rad2ChainCode(*p_dir);

            p_diffH++;
            p_diffV++;
            p_amp++;
            p_dir++;
            p_code++;
        }
    }
}


template <typename T>
void EdgeDetector::calcHorizontalDiffImage(
    const T* image,
    const int32_t width, const int32_t height, const int32_t stride_byte,
    const double* smoothFilter, const double* diffFilter, const int32_t fsize,
    const cv::Rect roi,
    double* diffImageH)
{
    assert(image != nullptr);
    assert(diffImageH != nullptr);
    assert(smoothFilter != nullptr);
    assert(diffFilter != nullptr);

    int32_t fradius = fsize / 2;

    int32_t stride_pix = stride_byte / sizeof(T);
    assert(stride_pix == width);

    std::unique_ptr< double[] > temp_buff(new double[stride_pix*height]());
    int32_t dst_stride_byte = stride_pix * sizeof(double);

    //垂直方向平滑化
    CSpatialFilter::ConvolutionV(image, width, height, stride_byte, smoothFilter, fsize, roi+ fradius, temp_buff.get(), dst_stride_byte);

    //水平方向微分
    CSpatialFilter::ConvolutionH(temp_buff.get(), width, height, dst_stride_byte, diffFilter, fsize, roi, diffImageH, dst_stride_byte);
}


template <typename T>
void EdgeDetector::calcVerticalDiffImage(
    const T* image,
    const int32_t width, const int32_t height, const int32_t stride_byte,
    const double* smoothFilter, const double* diffFilter, const int32_t fsize,
    const cv::Rect roi,
    double* diffImageV)
{
    assert(image != nullptr);
    assert(diffImageV != nullptr);
    assert(smoothFilter != nullptr);
    assert(diffFilter != nullptr);

    int32_t fradius = fsize / 2;

    int32_t stride_pix = stride_byte / sizeof(T);
    assert(stride_pix == width);

    std::unique_ptr< double[] > temp_buff(new double[stride_pix*height]());
    int32_t dst_stride_byte = stride_pix * sizeof(double);

    //水平方向平滑化
    CSpatialFilter::ConvolutionH(image, width, height, stride_byte, smoothFilter, fsize, roi+ fradius, temp_buff.get(), dst_stride_byte);

    //垂直方向微分
    CSpatialFilter::ConvolutionV(temp_buff.get(), width, height, dst_stride_byte, diffFilter, fsize, roi, diffImageV, dst_stride_byte);
}

uint8_t EdgeDetector::rad2ChainCode(const double _rad)
{
	double rad = _rad;
    //-PI～+PI～ → 0～+2PI
    if(rad < 0.0)
    {
        rad += 2.0 * M_PI;
    }

    const int32_t code = static_cast<int32_t>(rad / (M_PI * 0.25) + 0.5);

    const uint8_t edge_code = static_cast<uint8_t>(code % 8);

    return edge_code;
}

void EdgeDetector::getNeighborPixelIndexInEdgeDirection(const uint8_t edgeCode, const int32_t stride_pix, int32_t& nextIndex, int32_t& backIndex)
{
    assert(edgeCode < EDGEDIR_NUM);

    switch(edgeCode)
    {
    case EDGEDIR_RIGHT:       //右
    default:
        nextIndex = +1;
        backIndex = -1;
        break;

    case EDGEDIR_LOWERRIGHT:  //右下
        nextIndex = +stride_pix + 1;
        backIndex = -stride_pix - 1;
        break;

    case EDGEDIR_UNDER:       //下
        nextIndex = +stride_pix;
        backIndex = -stride_pix;
        break;

    case EDGEDIR_LOWERLEFT:   //左下
        nextIndex = +stride_pix - 1;
        backIndex = -stride_pix + 1;
        break;

    case EDGEDIR_LEFT:        //左
        nextIndex = -1;
        backIndex = +1;
        break;

    case EDGEDIR_UPPERLEFT:   //左上
        nextIndex = -stride_pix - 1;
        backIndex = +stride_pix + 1;
        break;

    case EDGEDIR_TOP:         //上
        nextIndex = -stride_pix;
        backIndex = +stride_pix;
        break;

    case EDGEDIR_UPPERRIGHT:  //右上
        nextIndex = -stride_pix + 1;
        backIndex = +stride_pix - 1;
        break;
    }
}

void EdgeDetector::calcEdgePeakImage(
    const double* const edgeAmp, const uint8_t* const edgeCode,
    const int32_t width, const int32_t height,    const int32_t stride_pix,
    const cv::Rect _roi,
    uint8_t* const edgePeak)
{
    assert(edgeAmp != nullptr);
    assert(edgeCode != nullptr);
    assert(edgePeak != nullptr);

    cv::Rect roi = _roi & cv::Rect(1, 1, width - 2, height - 2);
    //roi.Triming(1, 1, width - 2, height - 2);    //※全画面の場合１ピクセル内側とする

    const int32_t sx = roi.x;
    const int32_t sy = roi.y;
    const int32_t ex = roi.x + roi.width - 1;
    const int32_t ey = roi.y + roi.height - 1;


#pragma omp parallel for 
    for(int32_t y=sy; y<=ey; ++y)
    {
        const double *const amp = const_cast<double*>(&edgeAmp[y*stride_pix]);
        const uint8_t* const code = const_cast<uint8_t*>(&edgeCode[y*stride_pix]);
        uint8_t* const peak = &edgePeak[y*stride_pix];

        for(int32_t x=sx; x<=ex; ++x)
        {
            int32_t nextIndex = 0;
            int32_t backIndex = 0;
            getNeighborPixelIndexInEdgeDirection(code[x], stride_pix, nextIndex, backIndex);

            const double next_amp = amp[x + nextIndex];
            const double back_amp = amp[x + backIndex];

            //ピーク判定
            if( (amp[x] >= next_amp) && (amp[x] >= back_amp) )
            {
                peak[x] = HIGH;
            }
            else
            {
                peak[x] = LOW;
            }
        }
    }
}




int32_t EdgeDetector::getConnect(const int32_t* const sn)
{
    int32_t sum = 0;

    for(int32_t i=1; i<9; i+=2)
    {
        int32_t j = i + 1;
        int32_t k = i + 2;
        if(j > 8)
        {
            j -= 8;
        }
        if(k > 8)
        {
            k -= 8;
        }
        sum += (static_cast<int>(!sn[i]) - static_cast<int>(!(sn[i] | sn[j] | sn[k])));
    }

    return sum;
}
bool EdgeDetector::isDeletable(uint8_t* const edgePeak, const int32_t x, const int32_t y, const int32_t stride_pix)
{
    assert(edgePeak != nullptr);

    int32_t n[9] = {0};
    int32_t sn[9] = {0};
    int32_t ret = 0;
    int32_t sum = 0;
    int32_t temp = 0;

    n[0] = static_cast<int32_t>(edgePeak[stride_pix*(y-0) + (x+0)]);    // 中央
    n[1] = static_cast<int32_t>(edgePeak[stride_pix*(y-0) + (x+1)]);    // 右
    n[2] = static_cast<int32_t>(edgePeak[stride_pix*(y-1) + (x+1)]);    // 右上
    n[3] = static_cast<int32_t>(edgePeak[stride_pix*(y-1) + (x+0)]);    // 上
    n[4] = static_cast<int32_t>(edgePeak[stride_pix*(y-1) + (x-1)]);    // 左上
    n[5] = static_cast<int32_t>(edgePeak[stride_pix*(y+0) + (x-1)]);    // 左
    n[6] = static_cast<int32_t>(edgePeak[stride_pix*(y+1) + (x-1)]);    // 左下
    n[7] = static_cast<int32_t>(edgePeak[stride_pix*(y+1) + (x+0)]);    // 下
    n[8] = static_cast<int32_t>(edgePeak[stride_pix*(y+1) + (x+1)]);    // 右下

    //対象とする連結成分の注目画素(ここでは値が1の白の画素)であること
    if(n[0] != static_cast<int32_t>(HIGH))
    {
        return false;
    }

    for(int32_t i=0; i<9; i++)
    {
        if(n[i] == HIGH)
        {
            n[i] = 1;
            sn[i] = 1;
        }
        else if(n[i] == TEMP)
        {
            n[i] = -1;
            sn[i] = 1;
        }
        else
        {
            n[i] = 0;
            sn[i] = 0;
        }

        sum += sn[i];
    }

    //連結数が1でないものは削除対象外とする
    ret = getConnect(sn);
    if(ret != 1)
    {
        return false;
    }

    //連結数1かつ総和が2の場合は端点なので削除しない
    if(sum == 2)
    {
        return false;
    }

    //線幅2の線分の片側だけを削除する
    sum = 0;
    for ( int32_t i = 1; i <= 8; i++ )
    {
        if ( n[i] != -1 )
        {
            sum++;
        }
        else
        {
            temp = sn[i];
            sn[i] = 0;
            if ( getConnect( sn ) == 1 )
            {
                sum++;
            }
            sn[i] = temp;
        }
    }

    if ( sum != 8 )
    {
        return false;
    }

    return true;
}

void EdgeDetector::calcThinningImage(uint8_t* const edgePeak, const int32_t width, const int32_t height, const int32_t stride_pix, const cv::Rect _roi)
{
    assert(edgePeak != nullptr);

    //TODO : ROIは一つ内側から探索するべきか？
    cv::Rect roi = _roi & cv::Rect(1, 1, width - 2, height - 2);
    //roi.Triming(1, 1, width - 2, height - 2);

    const int32_t sx = roi.x;
    const int32_t sy = roi.y;
    const int32_t ex = roi.x + roi.width - 1;
    const int32_t ey = roi.y + roi.height - 1;

    int32_t del_num = 0;
    do
    {
        del_num = 0;

        for(int32_t y=sy; y<=ey; ++y)
        {
            uint8_t* const peak = &edgePeak[y*stride_pix];

            for(int32_t x=sx; x<=ex; ++x)
            {
                if(isDeletable(edgePeak, x, y, stride_pix))
                {
                    peak[x] = TEMP;
                    del_num++;
                }
            }
        }

        for(int32_t y=sy; y<=ey; ++y)
        {
            uint8_t* const peak = &edgePeak[y*stride_pix];

            for(int32_t x=sx; x<=ex; ++x)
            {
                if(peak[x] == TEMP)
                {
                    peak[x] = LOW;
                }
            }
        }

    }while(del_num > 0);
}




void EdgeDetector::calcEdgeSubpixelPoint(
    const int32_t x, const int32_t y,
    const double* const edgeAmp, const double* const edgeDir, const uint8_t* const edgeCode,
    const int32_t stride_pix, 
    EdgeVertex& edgePoint)
{
    assert(edgeAmp != nullptr);
    assert(edgeDir != nullptr);

#if 1
		//サブピクセルあり

		const double amp = edgeAmp[y*stride_pix + x];
		const double dir = edgeDir[y*stride_pix + x];
		const unsigned char code = edgeCode[y*stride_pix + x];

		int32_t next_index = 0;
		int32_t back_index = 0;
		getNeighborPixelIndexInEdgeDirection(code, stride_pix, next_index, back_index);

		const double next_amp = edgeAmp[stride_pix*y + x + next_index];
		const double back_amp = edgeAmp[stride_pix*y + x + back_index];

		//サブピクセル座標の算出
		const double a = (next_amp + back_amp) * 0.5 - amp;
		const double b = (back_amp - next_amp) * 0.5;
		double s = (fabs(a) < DBL_EPSILON) ? 0.0 : (-b * 0.5 / a);
		if(s > 1.0)
		{
			s = 1.0;
		}
		else if(s < -1.0)
		{
			s = -1.0;
		}
		else
		{
			;
		}

		edgePoint.pos.x = x - s * cos(code * M_PI * 0.25);
		edgePoint.pos.y = y - s * sin(code * M_PI * 0.25);
		edgePoint.magnitude = a*s*s + b*s + amp;
		edgePoint.orientation = dir;
#else
		//サブピクセルなし

		double amp = edgeAmp[y*stride_pix + x];
		double dir = edgeDir[y*stride_pix + x];

		edgePoint.pos.x = x;
		edgePoint.pos.y = y;
		edgePoint.magnitude = amp;
		edgePoint.orientation = dir;
#endif
}


void EdgeDetector::getNeighborDiffCoordinateInEdgeDirection(const uint8_t edgeCode, int32_t &diffX, int32_t &diffY)
{
    assert(edgeCode < EDGEDIR_NUM);

    switch(edgeCode)
    {
    case EDGEDIR_RIGHT:       //右
    default:
        diffX = +1;
        diffY = 0;
        break;

    case EDGEDIR_LOWERRIGHT:  //右下
        diffX = +1;
        diffY = +1;
        break;

    case EDGEDIR_UNDER:       //下
        diffX = 0;
        diffY = +1;
        break;

    case EDGEDIR_LOWERLEFT:   //左下
        diffX = -1;
        diffY = +1;
        break;

    case EDGEDIR_LEFT:        //左
        diffX = -1;
        diffY = 0;
        break;

    case EDGEDIR_UPPERLEFT:   //左上
        diffX = -1;
        diffY = -1;
        break;

    case EDGEDIR_TOP:         //上
        diffX = 0;
        diffY = -1;
        break;

    case EDGEDIR_UPPERRIGHT:  //右上
        diffX = +1;
        diffY = -1;
        break;
    }
}

void EdgeDetector::trackEdgePolygonCCW(
    const int32_t x, const int32_t y,
    uint8_t* const edgePeak,
    const double* const edgeAmp, const double* const edgeDir, const uint8_t* const edgeCode,
    const int32_t stride_pix,
    Polyline& polygon)
{
    assert(edgeAmp != nullptr);
    assert(edgeDir != nullptr);

    const int32_t start_x = x;
    const int32_t start_y = y;
    int32_t curt_x = start_x;
    int32_t curt_y = start_y;

    //反時計回り
    for (; ;)
    {
        const unsigned char code = edgeCode[stride_pix*curt_y + curt_x];

        //垂直方向を探索
        unsigned char code_v = (code + 2U) % 8U;
        int32_t diff_x = 0, diff_y = 0;
        getNeighborDiffCoordinateInEdgeDirection(code_v, diff_x, diff_y);
        int32_t next_x = curt_x + diff_x;
        int32_t next_y = curt_y + diff_y;

        if (edgePeak[stride_pix*next_y + next_x] != HIGH)
        {
            //垂直方向±45°を探索
            double min_diff_amp = DBL_MAX;
            int32_t min_x = 0;
            int32_t min_y = 0;
            for (int32_t i = 0; i < 2; i++)
            {
                code_v = (code + static_cast<unsigned char>(2 * i) + 1U) % 8U;
                getNeighborDiffCoordinateInEdgeDirection(code_v, diff_x, diff_y);
                next_x = curt_x + diff_x;
                next_y = curt_y + diff_y;

                if (edgePeak[stride_pix*next_y + next_x] != HIGH)
                {
                    continue;
                }

                //現在のエッジ強度との差
                const double diff_amp = fabs(edgeAmp[stride_pix*curt_y + curt_x] - edgeAmp[stride_pix*next_y + next_x]);
                if (diff_amp < min_diff_amp)
                {
                    min_diff_amp = diff_amp;
                    min_x = next_x;
                    min_y = next_y;
                }
            }

            if (min_diff_amp == DBL_MAX)
            {
                break;
            }
            next_x = min_x;
            next_y = min_y;
        }

        //エッジ点
        EdgeVertex point;
        calcEdgeSubpixelPoint(next_x, next_y, edgeAmp, edgeDir, edgeCode, stride_pix, point);
        polygon.push_back(point);
        //ピークの削除
        edgePeak[stride_pix*next_y + next_x] = LOW;

        if (next_x == start_x && next_y == start_y)
        {
            break;
        }

        curt_x = next_x;
        curt_y = next_y;
    }
}

void EdgeDetector::trackEdgePolygonCW(
    const int32_t x, const int32_t y,
    uint8_t* const edgePeak,
    const double* const edgeAmp, const double* const edgeDir, const uint8_t* const edgeCode,
    const int32_t stride_pix,
    Polyline& polygon)
{
    assert(edgeAmp != nullptr);
    assert(edgeDir != nullptr);

    const int32_t start_x = x;
    const int32_t start_y = y;
    int32_t curt_x = start_x;
    int32_t curt_y = start_y;

    //時計回り
    for (; ;)
    {
        const uint8_t code = edgeCode[stride_pix*curt_y + curt_x];

        //垂直方向を探索
        uint8_t code_v = (code + 6U) % 8U;
        int32_t diff_x = 0;
        int32_t diff_y = 0;
        getNeighborDiffCoordinateInEdgeDirection(code_v, diff_x, diff_y);
        int32_t next_x = curt_x + diff_x;
        int32_t next_y = curt_y + diff_y;

        if (edgePeak[stride_pix * next_y + next_x] != HIGH)
        {
            //垂直方向±45°を探索
            double min_diff_amp = DBL_MAX;
            int32_t min_x = 0;
            int32_t min_y = 0;
            for (int32_t i = 0; i < 2; i++)
            {
                code_v = (code + static_cast<uint8_t>(2 * i) + 5U) % 8U;
                getNeighborDiffCoordinateInEdgeDirection(code_v, diff_x, diff_y);
                next_x = curt_x + diff_x;
                next_y = curt_y + diff_y;

                if (edgePeak[stride_pix * next_y + next_x] != HIGH)
                {
                    continue;
                }

                //現在のエッジ強度との差
                const double diff_amp = fabs(edgeAmp[stride_pix * curt_y + curt_x] - edgeAmp[stride_pix * next_y + next_x]);
                if (diff_amp < min_diff_amp)
                    //if ((min_diff_amp - diff_amp) > DBL_EPSILON)
                {
                    min_diff_amp = diff_amp;
                    min_x = next_x;
                    min_y = next_y;
                }
            }

            if (min_diff_amp == DBL_MAX)
            {
                break;
            }

            next_x = min_x;
            next_y = min_y;
        }

        //エッジ点
        EdgeVertex point;
        calcEdgeSubpixelPoint(next_x, next_y, edgeAmp, edgeDir, edgeCode, stride_pix, point);
        polygon.push_back(point);
        //ピークの削除
        edgePeak[stride_pix*next_y + next_x] = LOW;


        if (next_x == start_x && next_y == start_y)
        {
            break;
        }

        curt_x = next_x;
        curt_y = next_y;
    }

}


void EdgeDetector::trackEdgePolygon(
    const int32_t x, const int32_t y,
    uint8_t* const edgePeak,
    const double* const edgeAmp, const double* const edgeDir, const uint8_t* const edgeCode,
    const int32_t stride_pix,
    Polylines& edgePolygons)
{
    assert(edgeAmp != nullptr);
    assert(edgeDir != nullptr);

    Polyline polygon;

    bool ret_CW = false;
    bool ret_CCW = false;

    //開始位置
    EdgeVertex point;
    calcEdgeSubpixelPoint(x, y, edgeAmp, edgeDir, edgeCode, stride_pix, point);
    polygon.push_back(point);

    //反時計回り
    trackEdgePolygonCCW(x, y, edgePeak, edgeAmp, edgeDir, edgeCode, stride_pix, polygon);
    std::reverse(polygon.begin(), polygon.end());

    //時計回り
    trackEdgePolygonCW(x, y, edgePeak, edgeAmp, edgeDir, edgeCode, stride_pix, polygon);

    //開始点のエッジを削除
    edgePeak[y*stride_pix+x] = LOW;

    edgePolygons.push_back(polygon);
}


void EdgeDetector::trackEdgePeak(
    uint8_t* const edgePeak,
    const double* const edgeAmp, const double* const edgeDir, const uint8_t* const edgeCode,
    const int32_t width, const int32_t height, const int32_t stride_pix, 
    const cv::Rect _roi, 
    Polylines& edgePolygons)
{
    assert(edgeAmp != nullptr);
    assert(edgeDir != nullptr);

    cv::Rect roi = _roi & cv::Rect(1, 1, width - 2, height - 2);

    const int32_t sx = roi.x;
    const int32_t sy = roi.y;
    const int32_t ex = roi.x + roi.width - 1;
    const int32_t ey = roi.y + roi.height - 1;

    for(int32_t y = sy; y <= ey; ++y)
    {
        uint8_t* peak = &edgePeak[y * stride_pix + sx];

        for(int32_t x = sx; x <= ex; ++x)
        {
            if(*peak == HIGH)
            {
                trackEdgePolygon(x, y, edgePeak, edgeAmp, edgeDir, edgeCode, stride_pix, edgePolygons);
            }
            peak++;
        }
    }
}

#if 0
void debug_dumpEdgePolygons(wchar_t *filepath, EdgePolygonList *edgePolygons)
{
    FILE *fp;
    _wfopen_s(&fp, filepath, L"w");

    int32_t poly_num = edgePolygons->size();
    for(int32_t i=0; i<poly_num; ++i)
    {
        EdgePolygon &poly = edgePolygons->at(i);

        int32_t pnt_num = poly.size();
        for(int32_t j=0; j<pnt_num; ++j)
        {
            EdgePoint64F &edge = poly.at(j);

            fprintf_s(fp, "%d,%d,%f,%f,%f,%f\n", i, j, edge.x, edge.y, edge.amp, edge.dir);
        }
    }

    fclose(fp);
}
#endif

void EdgeDetector::FilterLength(const Polylines& srcEdge, Polylines& dstEdge, const int32_t minLen)
{
    //assert(dstEdge != nullptr);

    if(minLen <= 0)
    {
        dstEdge = srcEdge;
        return;
    }

    for (Polylines::iterator it = const_cast<Polylines&>(srcEdge).begin();
        it != const_cast<Polylines&>(srcEdge).end();
        it++)
    {
        const int32_t num_points = static_cast<int32_t>(it->size());
        if (num_points >= minLen)
        {
            dstEdge.push_back(*it);
        }
    }
}

double EdgeDetector::calcDistanceSquare(const EdgeVertex&edge1, const EdgeVertex&edge2)
{
    const double dx = edge1.pos.x - edge2.pos.x;
    const double dy = edge1.pos.y    - edge2.pos.y;
    return dx*dx + dy*dy;
}

bool EdgeDetector::sort_value_less(const idx_val_type lhs, const idx_val_type rhs)
{
    return lhs.value < rhs.value;
}
bool EdgeDetector::sort_value_greater(const idx_val_type lhs, const idx_val_type rhs)
{
    return lhs.value > rhs.value;
}


#define CONNECT_TYPE_NOT    (-1)
#define CONNECT_TYPE_SS     (0)
#define CONNECT_TYPE_SE     (1)
#define CONNECT_TYPE_ES     (2)
#define CONNECT_TYPE_EE     (3)

int32_t EdgeDetector::isConnectPolygon(const Polyline& srcPolygon1, const Polyline& srcPolygon2, const double threDist, double* const distance)
{
    const int32_t size1 = static_cast<int32_t>(srcPolygon1.size());
    const int32_t size2 = static_cast<int32_t>(srcPolygon2.size());

    EdgeVertex edge1_s = srcPolygon1.at(0);
    EdgeVertex edge1_e = srcPolygon1.at(size1-1);
    EdgeVertex edge2_s = srcPolygon2.at(0);
    EdgeVertex edge2_e = srcPolygon2.at(size2-1);

    const double threDistSq = threDist * threDist;

    std::vector<idx_val_type> vec;
    vec.push_back(idx_val_type(CONNECT_TYPE_SS, calcDistanceSquare(edge1_s, edge2_s), -1));
    vec.push_back(idx_val_type(CONNECT_TYPE_SE, calcDistanceSquare(edge1_s, edge2_e), -1));
    vec.push_back(idx_val_type(CONNECT_TYPE_ES, calcDistanceSquare(edge1_e, edge2_s), -1));
    vec.push_back(idx_val_type(CONNECT_TYPE_EE, calcDistanceSquare(edge1_e, edge2_e), -1));

    std::sort(vec.begin(), vec.end(), sort_value_less);

    if(vec.at(0).value < threDistSq)
    {
        *distance = vec.at(0).value;
        return vec.at(0).index;
    }
    else
    {
        return CONNECT_TYPE_NOT;
    }
}

bool EdgeDetector::connectPolygon(const Polyline& srcPolygon1, const Polyline& srcPolygon2, Polyline& dstPolygon, const int32_t type)
{
    const int32_t size1 = static_cast<int32_t>(srcPolygon1.size());
    const int32_t size2 = static_cast<int32_t>(srcPolygon2.size());

    const EdgeVertex edge1_s = srcPolygon1.at(0);
    const EdgeVertex edge1_e = srcPolygon1.at(size1-1);
    const EdgeVertex edge2_s = srcPolygon2.at(0);
    const EdgeVertex edge2_e = srcPolygon2.at(size2-1);


    if(type == CONNECT_TYPE_SS)
    {
        dstPolygon = srcPolygon2;
        std::reverse(dstPolygon.begin(), dstPolygon.end());
        dstPolygon.insert(dstPolygon.end(), srcPolygon1.begin(), srcPolygon1.end());
        return true;
    }
    else if(type == CONNECT_TYPE_SE)
    {
        dstPolygon = srcPolygon2;
        dstPolygon.insert(dstPolygon.end(), srcPolygon1.begin(), srcPolygon1.end());
        return true;
    }
    else if(type == CONNECT_TYPE_ES)
    {
        dstPolygon = srcPolygon1;
        dstPolygon.insert(dstPolygon.end(), srcPolygon2.begin(), srcPolygon2.end());
        return true;
    }
    else if(type == CONNECT_TYPE_EE)
    {
        dstPolygon = srcPolygon2;
        std::reverse(dstPolygon.begin(), dstPolygon.end());
        dstPolygon.insert(dstPolygon.begin(), srcPolygon1.begin(), srcPolygon1.end());
        return true;
    }
    else
    {
        return false;
    }
}

#define CONNECTED        (1U)
#define DISCONNECTED    (0U)
void EdgeDetector::ConnectEdges(const Polylines& srcEdges, Polylines& dstEdges, const double threDist)
{
    //assert(dstEdges != nullptr);
    
    if(threDist < DBL_EPSILON)
    {
        dstEdges = srcEdges;
        return;
    }

    //結合済みフラグ（1:結合済み, 0:未結合）
    const int32_t poly_size = static_cast<int32_t>(srcEdges.size());
    std::unique_ptr<int32_t[]> connect_flag(new int32_t[poly_size]());

    //基準ポリゴン
    for(int32_t i=0; i<poly_size; ++i)
    {
        if(connect_flag[i] == CONNECTED)
        {
            continue;
        }

        connect_flag[i] = CONNECTED;

        Polyline target = srcEdges[i];

        for (;;)
        {
            //全てのエッジとの距離を算出
            std::vector<idx_val_type> vec;
            for(int32_t j=0; j<poly_size; ++j)
            {
                if(i == j){continue;}
                if(connect_flag[j] == CONNECTED){continue;}

                double dist = 0.0;
                int32_t type = isConnectPolygon(target, srcEdges[j], threDist, &dist);
                if(type != CONNECT_TYPE_NOT)
                {
                    vec.push_back(idx_val_type(j, dist, type));
                }
            }

            if(vec.size() > 0)
            {
                std::sort(vec.begin(), vec.end(), sort_value_less);

                const int32_t jj = vec.at(0).index;
                const int32_t type = vec.at(0).type;

                Polyline connected;
                const bool is_connected = connectPolygon(target, srcEdges[jj], connected, type);
                if(is_connected)
                {
                    connect_flag[jj] = CONNECTED;
                    target = connected;
                }
            }
            else
            {
                break;
            }
        }

        dstEdges.push_back(target);
    }
}


void EdgeDetector::copyPolygon(const Polyline&srcPolygon, Polyline& copyPolygon, const int32_t sx, const int32_t ex)
{
    for(int32_t i=sx; i<=ex; ++i)
    {
        copyPolygon.push_back(srcPolygon.at(i));
    }
}


bool EdgeDetector::getSplitPoint(double* const arrCosQ, const int32_t size, const int32_t k, const double threCosQ, int32_t* const idx)
{
    double max_cosQ = -DBL_MAX;
    int32_t max_idx = -1;

    for(int32_t i=k; i<size-k; ++i)
    {
        if( arrCosQ[i] > threCosQ &&
            arrCosQ[i] > max_cosQ)
        {
            max_cosQ = arrCosQ[i];
            max_idx = i;
        }
    }

    if(max_idx != -1)
    {
        //非極値抑制
        for(int32_t i=max_idx - k; i<max_idx + k; ++i)
        {
            arrCosQ[i] = -1;
        }

        *idx = max_idx;
        return true;
    }
    else
    {
        return false;
    }
}


void EdgeDetector::splitEdgePolygon(const Polyline& srcPolygon, Polylines& dstPolygonList, const double threDeg, const int32_t k)
{
    const int32_t num = static_cast<int32_t>(srcPolygon.size());
    std::unique_ptr<double[]> arr_cosQ(new double[num]());

    //各点での角度
    for(int32_t i=k; i<num-k; ++i)
    {
        const EdgeVertex pnt0 = srcPolygon.at(i);
        const EdgeVertex pnt1 = srcPolygon.at(i-k);
        const EdgeVertex pnt2 = srcPolygon.at(i+k);

        const cv::Vec2d vec1 = cv::Vec2d(pnt1.pos - pnt0.pos);
        const cv::Vec2d vec2 = cv::Vec2d(pnt2.pos - pnt0.pos);

        const double cosQ = vec1.dot(vec2) / (norm(vec1) * norm(vec2) + 1e-12);
        arr_cosQ[i] = cosQ;
    }

    const double threCosQ = cos((180.0-threDeg)/180.0*M_PI);

    std::vector<int32_t> split_id;
    for(;;)
    {
        int32_t idx = 0;
        const bool ret = getSplitPoint(arr_cosQ.get(), num, k, threCosQ, &idx);

        if(ret != true)
        {
            break;
        }

        split_id.push_back(idx);
    }


    sort(split_id.begin(), split_id.end());

    //分離
    if(split_id.empty())
    {
        dstPolygonList.push_back(srcPolygon);
    }
    else
    {
        int32_t sx = 0;
        for(size_t i=0; i<split_id.size(); ++i)
        {
            const int32_t ex = split_id.at(i);

            Polyline polygon;
            copyPolygon(srcPolygon, polygon, sx, ex);

            dstPolygonList.push_back(polygon);

            sx = ex + 1;
        }

        Polyline polygon;
        copyPolygon(srcPolygon, polygon, sx, num-1);
        dstPolygonList.push_back(polygon);
    }
}

void EdgeDetector::SplitEdges(const Polylines&srcEdges, Polylines& dstEdges, const double threDeg, const int32_t step)
{
    //assert(dstEdges != nullptr);

    if(threDeg < DBL_EPSILON || step == 0)
    {
        dstEdges = srcEdges;
        return;
    }

    for(size_t i=0; i<srcEdges.size(); ++i)
    {
        Polylines splited;
        splitEdgePolygon(srcEdges.at(i), splited, threDeg, step);

        for(size_t j=0; j<splited.size(); ++j)
        {
            dstEdges.push_back(splited.at(j));
        }
    }
}


void EdgeDetector::maskingPeakmap(
    uint8_t* const edgePeak, 
    const int32_t width, const int32_t height, const int32_t stride_pix,
    const cv::Rect _roi,
    const uint8_t* const mask)
{
    assert(edgePeak != nullptr);
    assert(mask != nullptr);

    cv::Rect roi = _roi & cv::Rect(0, 0, width - 1, height - 1);
    //roi.Triming(0, 0, width - 1, height - 1);

    const int32_t sx = roi.x;
    const int32_t sy = roi.y;
    const int32_t ex = roi.x + roi.width - 1;
    const int32_t ey = roi.y + roi.height - 1;


#pragma omp parallel for 
    for(int32_t y = sy; y <= ey; ++y)
    {
        uint8_t* ptr_peak = &edgePeak[y * stride_pix + sx];
        const uint8_t* ptr_mask = &mask[y * stride_pix + sx];

        for(int32_t x = sx; x <= ex; ++x)
        {
            *ptr_peak = (*ptr_peak) & (*ptr_mask);

            ptr_peak++;
            ptr_mask++;
        }
    }
}

void EdgeDetector::filterByEdgeDir(
    uint8_t* const edgePeak,
    double* const edgeDir,
    const int32_t width, const int32_t height, const int32_t stride_pix,
    const cv::Rect _roi,
    const double minDirDeg, const double maxDirDeg)
{
    if(fabs(maxDirDeg-minDirDeg) <= DBL_EPSILON)
    {
        return;
    }

    std::vector<double> min_dir_degs;
    std::vector<double> max_dir_degs;
    EdgeDetector::ConvertDegRange(minDirDeg, maxDirDeg, &min_dir_degs, &max_dir_degs);

    const int32_t dir_num = static_cast<int32_t>(min_dir_degs.size());
    std::vector<double> min_dir_rad;
    min_dir_rad.reserve(dir_num);
    std::vector<double> max_dir_rad;
    max_dir_rad.reserve(dir_num);
    for(int32_t i=0; i<dir_num; ++i)
    {
        min_dir_rad.push_back(min_dir_degs.at(i) / 180.0*M_PI);
        max_dir_rad.push_back(max_dir_degs.at(i) / 180.0 * M_PI);
    }

    cv::Rect roi = _roi & cv::Rect(1, 1, width - 2, height - 2);
    //roi.Triming(1, 1, width - 2, height - 2);

    const int32_t sx = roi.x;
    const int32_t sy = roi.y;
    const int32_t ex = roi.x + roi.width - 1;
    const int32_t ey = roi.y + roi.height - 1;

    for(int32_t y = sy; y <= ey; ++y)
    {
        uint8_t* peak = &edgePeak[y * stride_pix + sx];
        double *dir = &edgeDir[y * stride_pix + sx];

        for(int32_t x = sx; x <= ex; ++x)
        {
            if(*peak == HIGH)
            {
                bool ret = false;

                //いずれかの範囲に入っていればＯＫ
                for(int32_t i=0; i<dir_num; ++i)
                {
                    if(*dir >= min_dir_rad.at(i) && *dir <= max_dir_rad.at(i))
                    {
                        ret = true;
                    }
                }

                if(ret != true)
                {
                    *peak = LOW;
                }
            }

            peak++;
            dir++;
        }
    }

}


void EdgeDetector::ConvertDegRange(
    const double _srcMin, const double _srcMax,
    std::vector<double>* const dstMins,
    std::vector<double>* const dstMaxs)
{
    if (fabs(_srcMax - _srcMin) < DBL_EPSILON)
    {
        return;
    }

    double srcMin = fmod(_srcMin, 360.0);
    double srcMax = fmod(_srcMax, 360.0);


    if (srcMin < -180.0 && srcMax < -180.0)
    {
        srcMin += 360.0;
        srcMax += 360.0;
        std::swap(srcMin, srcMax);

        dstMins->push_back(srcMin);
        dstMaxs->push_back(srcMax);
    }
    else if (srcMin > 180.0 && srcMax > 180.0)
    {
        srcMin -= 360.0;
        srcMax -= 360.0;
        std::swap(srcMin, srcMax);

        dstMins->push_back(srcMin);
        dstMaxs->push_back(srcMax);
    }
    else if (srcMin < -180.0 && srcMax > -180.0)
    {
        double min = srcMin;
        double max = -180.0;
        min += 360.0;
        max += 360.0;

        dstMins->push_back(min);
        dstMaxs->push_back(max);

        dstMins->push_back(-180.0);
        dstMaxs->push_back(srcMax);
    }
    else if (srcMin < 180.0 && srcMax > 180.0)
    {
        dstMins->push_back(srcMin);
        dstMaxs->push_back(180.0);

        double min = 180.0;
        double max = srcMax;
        min -= 360.0;
        max -= 360.0;

        dstMins->push_back(min);
        dstMaxs->push_back(max);
    }
    else
    {
        //何もしない
        dstMins->push_back(srcMin);
        dstMaxs->push_back(srcMax);
    }
}


void EdgeDetector::HysteresisThreshold(
    uint8_t* edgePeak,
    const double* edgeAmp,
    const int32_t width, const int32_t height, const int32_t stride_pix,
    const cv::Rect _roi,
    const double lowThre,
    const double highThre)
{
    assert(edgePeak != nullptr);
    assert(edgeAmp != nullptr);
    //assert(highThre >= lowThre);

    // NMS等と同様、境界1pxは探索から外す
    cv::Rect roi = _roi & cv::Rect(1, 1, width - 2, height - 2);
    //roi.Triming(1, 1, width - 2, height - 2);

    const int32_t sx = roi.x;
    const int32_t sy = roi.y;
    const int32_t ex = roi.x + roi.width - 1;
    const int32_t ey = roi.y + roi.height - 1;

    // 0: non, 1: weak, 2: strong
    std::vector<uint8_t> state(static_cast<size_t>(width) * height, 0);
    std::queue<std::pair<int32_t, int32_t>> q;

    // 1) 初期ラベリング（NMSでHIGHの画素のみ対象）
    for (int32_t y = sy; y <= ey; ++y)
    {
        const double* a = &edgeAmp[y * stride_pix];
        uint8_t* pk = &edgePeak[y * stride_pix];
        uint8_t* st = &state[y * stride_pix];

        for (int32_t x = sx; x <= ex; ++x)
        {
            if (pk[x] != HIGH) continue;          // NMSで非候補は対象外

            const double v = a[x];
            if (v >= highThre) { st[x] = 2; q.emplace(x, y); }  // strong
            else if (v >= lowThre) { st[x] = 1; }               // weak
            // lowThre未満は non (=0)
        }
    }

    // 8近傍
    static const int dx[8] = { -1,0,1,-1,1,-1,0,1 };
    static const int dy[8] = { -1,-1,-1,0,0,1,1,1 };

    // 2) strong からBFSで weak を昇格（KEEP）
    while (!q.empty())
    {
        const auto [cx, cy] = q.front();
        q.pop();

        for (int k = 0; k < 8; ++k)
        {
            const int nx = cx + dx[k];
            const int ny = cy + dy[k];
            if (nx < sx || nx > ex || ny < sy || ny > ey) continue;

            uint8_t& ns = state[ny * stride_pix + nx];
            if (ns == 1) {
                ns = 2;                 // weak → strong(KEEP) に昇格
                q.emplace(nx, ny);
            }
        }
    }

    // 3) 出力確定：KEEP(=2)のみ HIGH、他は LOW へ
    for (int32_t y = sy; y <= ey; ++y)
    {
        uint8_t* pk = &edgePeak[y * stride_pix];
        uint8_t* st = &state[y * stride_pix];
        for (int32_t x = sx; x <= ex; ++x)
        {
            pk[x] = (st[x] == 2) ? HIGH : LOW;
        }
    }
}
