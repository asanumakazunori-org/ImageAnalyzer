#pragma once

#pragma warning(push)
#pragma warning(disable: 26495) // メンバー未初期化
#pragma warning(disable: 26451) // インデックス範囲外
#pragma warning(disable: 26439) // noexcept関連
#pragma warning(disable: 26440) // noexcept関連
#pragma warning(disable: 6287)  // 無効なfor-loop定義
#pragma warning(disable: 6001)  // 使用前の変数アクセス
#include <opencv2/opencv.hpp>
#pragma warning(pop)

#include <vector>

namespace ImageProc
{
    // ---- 1) エッジ点（Edgel / Edge vertex） ------------------------------------
    // 用語：position(=cv::Point2d), orientation[rad] ∈ [-π, π], magnitude(|∇I|)
    struct EdgeVertex {
        cv::Point2d pos;     // subpixel OK
        double      orientation; // radians [-pi, pi]
        double      magnitude;   // gradient magnitude (|∇I|)

        EdgeVertex() = default;
        EdgeVertex(double x, double y, double theta, double mag)
            : pos{ x, y }, orientation(theta), magnitude(mag) {
        }
        EdgeVertex(const cv::Point2d& p, double theta, double mag)
            : pos{ p }, orientation(theta), magnitude(mag) {
        }
    };

    // ---- 2) エッジ線（Polyline）とその集合（Polylines） -------------------------
    using Polyline = std::vector<EdgeVertex>;
    using Polylines = std::vector<Polyline>;

	enum EDGEPEAK_TYPE
	{
		LOW     =    0x00,            //非ピーク
		HIGH    =    0xFF,            //ピーク
		TEMP    =    0xF0,
	};

	///@brief エッジ方向コード
	enum EDGEDIR_CODE
	{
		EDGEDIR_RIGHT        = 0x00,        //右
		EDGEDIR_LOWERRIGHT   = 0x01,        //右下
		EDGEDIR_UNDER        = 0x02,        //下
		EDGEDIR_LOWERLEFT    = 0x03,        //左下
		EDGEDIR_LEFT         = 0x04,        //左
		EDGEDIR_UPPERLEFT    = 0x05,        //左上
		EDGEDIR_TOP          = 0x06,        //上
		EDGEDIR_UPPERRIGHT   = 0x07,        //右上

		EDGEDIR_NUM           = 0x08
	};

    struct idx_val_type
    {
        int32_t index;
        double value;
        int32_t type;

        idx_val_type(const int32_t _index, const double _value, const int32_t _type) : index(_index), value(_value), type(_type){}
    };



    class EdgeDetector
    {
    public:

        /*!
         * @brief Canny法によるエッジ検出
         *
         * @param [in] src              入力画像（グレー画像のみ）8U or 16U, 1ch
         * @param [in] sigma            ガウスフィルタ係数（default = 2.0）
         * @param [in] lowThre          ヒステリシス下限閾値(0 ~ 255)
         * @param [in] highThre         ヒステリシス上限閾値(0 ~ 255)
         * @param [in] roi              ROI
         * @param [in] mask             マスク画像(0xFF/0x00)
         * @param [in] minDirDeg        勾配フィルタ下限値
         * @param [in] maxDirDeg        勾配フィルタ上限値
         * @param [out] edgePolygons    エッジ線リスト
         *
         * @retval                      0 : 成功
         */
        static int32_t detect(
            const cv::Mat& src,
            Polylines& outEdges,
            const double sigma,
            const int32_t lowThre,
            const int32_t highThre,
            const cv::Rect& roi,
            const cv::Mat& mask = {},
            const double minDir = 0.0,
            const double maxDir = 0.0);

    private:
        /**
         * @brief ガウス微分（x方向）
         * 
         * @param [in] srcGray CV_8U or CV_16U
         * @param [out] gradY CV_32F, same size as src
         * @param [in] sigma
         * @param [in] roi  empty -> whole image
         */ 
        static void calcHorizontalDiffImage(
            const cv::Mat& srcGray,
            cv::Mat& gradX,
            const double sigma,
            const cv::Rect& roi = {}
        );

        /**
         * @brief ガウス微分（y方向）
         *
         * @param [in] srcGray CV_8U or CV_16U
         * @param [out] gradY CV_32F, same size as src
         * @param [in] sigma
         * @param [in] roi  empty -> whole image
         */
        static void calcVerticalDiffImage(
            const cv::Mat& srcGray,
            cv::Mat& gradY,             // CV_32F
            const double sigma,
            const cv::Rect& roi = {}
        );

        // 勾配強度・方向・方位コード（4/8方向量子化）
        static void calcEdgeAmpDir(
            const cv::Mat& srcGray,     // CV_8U or CV_16U
            cv::Mat& edgeAmp,           // CV_32F (||∇I||)
            cv::Mat& edgeDir,           // CV_32F (radian, [-pi, pi))
            cv::Mat& edgeCode,          // CV_8U  (0..3 or 0..7)
            double sigma,
            const cv::Rect& roi = {},
            int quantizeDirections = 4   // 4 or 8
        );

    private:
        /*!
        * @brief 水平方向の微分画像の算出
        * @note 垂直方向に平滑化した後、水平方向に微分する
        *
        * @param [in] image                入力画像（グレー）
        * @param [in] width                画像サイズ幅[pixel]
        * @param [in] height               画像サイズ高さ[pixel]
        * @param [in] stride_byte          画像ストライド[byte]
        * @param [in] smoothFilter         平滑化フィルタ係数
        * @param [in] diffFilter           微分フィルタ係数
        * @param [in] fsize                フィルタサイズ
        * @param [in] roi                  ROI
        * @param [out] diffImageH          水平方向微分画像
        */
        template <typename T>
        static void calcHorizontalDiffImage(
            const T* const image,
            const int32_t width, const int32_t height, const int32_t stride_byte,
            const double* const smoothFilter, const double* const diffFilter, const int32_t fsize,
            const cv::Rect roi,
            double* const diffImageH);

        /*!
        * @brief 垂直方向の微分画像の算出
        * @note 水平方向に平滑化した後、垂直方向に微分する
        *
        * @param [out] diffImageV        垂直方向微分画像
        */
        template <typename T>
        static void calcVerticalDiffImage(
            const T* const image,
            const int32_t width, const int32_t height, const int32_t stride_byte,
            const double* const smoothFilter, const double* const diffFilter, const int32_t fsize,
            const cv::Rect roi,
            double* const diffImageV);

        /*!
        * @brief エッジ強度、角度を算出する
        *
        * @param [in] image             入力画像（グレー）
        * @param [in] width             画像サイズ幅[pixel]
        * @param [in] height            画像サイズ高さ[pixel]
        * @param [in] stride_byte       画像ストライド[byte]
        * @param [in] smoothFilter      平滑化フィルタ係数
        * @param [in] diffFilter        微分フィルタ係数
        * @param [in] fsize             フィルタサイズ
        * @param [in] roi               ROI
        *
        * @param [out] edgeAmp          エッジ強度画像
        * @param [out] edgeDir          エッジ勾配画像（ラジアン）
        * @param [out] edgeCode         エッジコード画像
        *
        * @param [in] mode              モード 0:水平垂直、1:水平 2:垂直
        */
        template <typename T>
        static void calcEdgeAmpDir(
            const T* const image,
            const int32_t width, const int32_t height, const int32_t stride_byte,
            const double* const smoothFilter, const double* const diffFilter, const int32_t fsize,
            const cv::Rect _roi,
            double* const edgeAmp, double* const edgeDir, unsigned char* const edgeCode,
            const int32_t mode = 0);

        /*!
        * @brief エッジコード方向の前後に隣接する画素のインデックスを取得する
        *
        * @param [in] edgeCode          注目画素のエッジコード
        * @param [in] stride_pix        画像のストライド
        *
        * @param [out] nextIndex        正方向の画素インデックス
        * @param [out] backIndex        逆方向の画素インデックス
        */
        static void getNeighborPixelIndexInEdgeDirection(
			const uint8_t edgeCode, const int32_t stride_pix, int32_t& nextIndex, int32_t& backIndex);

        /*!
        * @brief エッジピークを検出する
        *
        * @param [in] edgeAmp           エッジ強度画像
        * @param [in] edgeCode          エッジコード
        * @param [in] width             画像サイズ幅[pixel]
        * @param [in] height            画像サイズ高さ[pixel]
        * @param [in] stride_pix        ストライド
        * @param [in] roi               ROI
        * @param [out] edgePeak         エッジピーク画像
        *
        * @note 下限閾値よりも低いエッジ強度はエッジ点としない
        * @note ROIの範囲で処理するので画像高さは不要
        */
        static void calcEdgePeakImage(
            const double* const edgeAmp, const uint8_t* const edgeCode,
            const int32_t width, const int32_t height, const int32_t stride_pix,
            const cv:: Rect _roi,
            uint8_t* const edgePeak);

        /*!
        * @brief 連結数を取得する
        *
        * @param [in] sn
        */
        static int32_t getConnect(const int32_t* const sn);

        /*!
        * @brief 削除点か判別する
        *
        * @param [in] edgePeak            エッジピーク画像
        * @param [in] x
        * @param [in] y
        * @param [in] stride_pix
        */
        static bool isDeletable(uint8_t* const edgePeak, const int32_t x, const int32_t y, const int32_t stride_pix);

        /*!
        * @brief 細線化する
        *
        * @param [in/out] edgePeak      エッジピーク画像
        * @param [in] width             画像サイズ幅[pixel]
        * @param [in] height            画像サイズ高さ[pixel]
        * @param [in] stride_pix        ストライド
        * @param [in] roi               ROI
        */
        static void calcThinningImage(uint8_t* const edgePeak, const int32_t width, const int32_t height, const int32_t stride_pix, const cv::Rect _roi);

        /*!
        * @brief サブピクセル精度でエッジ座標を算出する
        *
        * @param [in] x                  着目X座標
        * @param [in] y                  着目Y座標
        * @param [in] edgeAmp            エッジ強度
        * @param [in] edgeDir            エッジ方向
        * @param [in] edgeCode           エッジコード
        * @param [in] stride             画像ストライド
        * @param [out] edgePoint         エッジ点情報
        */
        static void calcEdgeSubpixelPoint(
            const int32_t x, const int32_t y,
            const double* const edgeAmp, const double* const edgeDir, const uint8_t* const edgeCode,
            const int32_t stride_pix, 
            EdgeVertex& edgePoint);

        /*!
        * @brief チェインコード方向に隣接する画素までの差分座標を取得する
        *
        * @param [in] edgeCode        注目画素のエッジコード
        * @param [in] stride          画像のストライド
        * @param [out] diffX          隣接画素までの差分x座標
        * @param [out] diffY          隣接画素までの差分y座標
        */
        static void getNeighborDiffCoordinateInEdgeDirection(
			const uint8_t edgeCode, int32_t &diffX, int32_t &diffY);


        /*!
        * @brief エッジピークを追尾する(反時計周り)
        *
        * ToDo 記載する
        */
        static void trackEdgePolygonCCW(
            const int32_t x, const int32_t y,
            uint8_t* const edgePeak,
            const double* const edgeAmp, const double* const edgeDir, const uint8_t* const edgeCode,
            const int32_t stride_pix,
            Polyline&  polygon);
        /*!
        * @brief エッジラインを追尾する(時計周り)
        *
        * ToDo 記載する
        */
        static void trackEdgePolygonCW(
            const int32_t x, const int32_t y,
            uint8_t* const edgePeak,
            const double* const edgeAmp, const double* const edgeDir, const uint8_t* const edgeCode,
            const int32_t stride_pix,
            Polyline& polygon);

        /*!
        * @brief エッジラインを追尾する(時計周り)
        *
        * ToDo 記載する
        */
        static void trackEdgePolygon(
            const int32_t x, const int32_t y,
            uint8_t* const edgePeak,
            const double* const edgeAmp, const double* const edgeDir, const uint8_t* const edgeCode,
            const int32_t stride_pix,
            Polylines& edgePolygons);
		
	public:
        /*!
        * @brief エッジピークを追尾する
        *
        * @param [in] edgePeak           エッジピーク画像
        * @param [in] edgeAmp            エッジ強度画像
        * @param [in] edgeDir            エッジ勾配画像
        * @param [in] edgeCode           エッジコード				
        * @param [in] stride             ストライド				
        * @param [in] roi                ROI						
        * @param [out] edgePolygons      エッジポリゴンのリスト
        */
        static void trackEdgePeak(
            uint8_t* const edgePeak,
            const double* const edgeAmp, const double* const edgeDir, const uint8_t* const edgeCode,
            const int32_t width, const int32_t height, const int32_t stride_pix,
            const cv::Rect _roi, 
            Polylines& edgePolygons);

	
        /*!
        * @brief ラジアンをチェインコードに変換する
        *
        * @param [in] rad -PI～+PI
        *
        * @return チェインコード
        *
        * @note 画像座標系で以下のコード順
        *  5 6 7
        *  4 * 0
        *  3 2 1
        */
        static uint8_t rad2ChainCode(const double rad);

	private:
        /*!
         * @brief エッジピーク画像をマスキングする
         * @note  マスク内のエッジが有効（逆がよいか？）
         *
         * @param [in/out] edgePeak
         */
        static void maskingPeakmap(
            uint8_t* const edgePeak, 
            const int32_t width, const int32_t height, const int32_t stride_pix,
            const cv::Rect _roi,
            const uint8_t* const mask);

        /*!
         * @brief エッジ勾配でフィルタをかける
         */
        static void filterByEdgeDir(
            uint8_t* const edgePeak,
            double* const edgeDir,
            const int32_t width, const int32_t height, const int32_t stride_pix,
            const cv::Rect _roi,
            const double minDirDeg, const double maxDirDeg);


    public:
        /*!
         * @brief 近接ポリゴンを接続する
         *
         * @param [in] srcEdges         結合前ポリゴン
         * @param [out] dstEdges        結合後ポリゴン
         * @param [in] threDist         結合閾値
         */
        static void ConnectEdges(
			const Polylines& srcEdges,
            Polylines& dstEdges, const double threDist);


        /*!
         * @brief ポリゴンを変曲点で分離する
         * 
         * @param [in] srcEdges         分離前ポリゴン
         * @param [out] dstEdges        分離後ポリゴン
         * @param [in] threDeg          分離閾値 時系列にpos0->pos1に対する、pos1->pos2のベクトルの変位角度が、threDeg以上になったときに分離される。
         * @param [in] step             前後何エッジ点を参照して角度算出するか？
         */
        static void SplitEdges(
			const Polylines&srcEdges,
            Polylines& dstEdges, const double threDeg=45.0, const int32_t step=20);


        /*!
        * @brief エッジポリゴンを長さでフィルタリングする
        *
        * @param [in] srcEdge           入力エッジ
        * @param [in] minLen            フィルタ閾値（下限）
        * @param [out] dstEdge          フィルタリング結果
        */
        static void FilterLength(
			const Polylines& srcEdge, Polylines& dstEdge, const int32_t minLength);


  
    private:
        static double calcDistanceSquare(const EdgeVertex& edge1, const EdgeVertex& edge2);
        static bool sort_value_less(const idx_val_type lhs, const idx_val_type rhs);
        static bool sort_value_greater(const idx_val_type lhs, const idx_val_type rhs);
        static int32_t isConnectPolygon(const Polyline& srcPolygon1, const Polyline& srcPolygon2, const double threDist, double* const distance);
        static bool connectPolygon(const Polyline& srcPolygon1, const Polyline& srcPolygon2, Polyline& dstPolygon, const int32_t type);
        static void copyPolygon(const Polyline& srcPolygon, Polyline& copyPolygon, const int32_t sx, const int32_t ex);
        static bool getSplitPoint(double* const arrCosQ, const int32_t size, const int32_t k, const double threCosQ, int32_t* const idx);

        /*!
         * @brief エッジポリゴンを分離する
         *
         * @param [in] srcPolygon        分離対象のポリゴン
         * @param [in] dstPolygonList    分離後のポリゴンリスト
         * @param [in] threDeg           分離閾値
         * @param [in] k                
         */
        static void splitEdgePolygon(const Polyline& srcPolygon, Polylines& dstPolygonList, const double threDeg, const int32_t k);


        /*!
         * @note -210~-150 →  -180~-150, +150~+180に変換する
         */
        static void ConvertDegRange(
            const double srcMinDeg,
            const double srcMaxDeg,
            std::vector<double>* const dstMinDegs,
            std::vector<double>* const dstMaxDegs);


        // ヒステリシス二値化（NMS→ヒステリシスの正しい順序に合わせて edge_peak を更新）
        static void HysteresisThreshold(
            uint8_t* edgePeak,                // [in,out] NMSのピークマップ(0/255) → ヒステリシス確定に更新
            const double* edgeAmp,            // [in]     勾配強度（同サイズ）
            const int32_t width, const int32_t height, const int32_t stride_pix,
            const cv::Rect _roi,            // [in]     ROI（内部で1px内側にトリム）
            const double lowThre,             // [in]     低閾値（edgeAmpと同スケール）
            const double highThre);           // [in]     高閾値（edgeAmpと同スケール）

    };
}

