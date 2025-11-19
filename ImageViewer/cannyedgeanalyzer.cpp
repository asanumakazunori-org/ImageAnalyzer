// cannyedgeanalyzer.cpp
#include "cannyedgeanalyzer.h"

#include <cmath>                // std::cos, std::sin, std::sqrt
#include <opencv2/imgproc.hpp>

using namespace ImageProc;

CannyEdgeAnalyzer::CannyEdgeAnalyzer(QObject* parent)
    : IaImageAnalyzer(parent)
{
}

// 解析本体：cv::Mat からグレースケール画像を作り、ImageProc::EdgeDetector を呼び出す。
void CannyEdgeAnalyzer::execute(const cv::Mat& originalImage)
{
    // 前回結果をクリア
    m_edges.clear();
    m_minMag = 0.0;
    m_maxMag = 1.0;

    if (originalImage.empty()) {
        return;
    }

    // EdgeDetector::detect は「1ch（8bit or 16bit）」のグレー画像を想定している。
    // originalImage がカラーの場合はここで毎回グレーに変換する。
    cv::Mat gray;

    if (originalImage.channels() == 1 &&
        (originalImage.depth() == CV_8U || originalImage.depth() == CV_16U))
    {
        // 既にグレー画像（8/16bit）ならそのまま使う
        gray = originalImage;
    }
    else
    {
        cv::Mat src = originalImage.clone();

        // 4ch（BGRA）の場合はアルファを捨てて 3ch BGR に
        if (src.channels() == 4) {
            cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);
        }

        // depth が 8U/16U 以外なら 8U にスケーリング
        if (src.depth() != CV_8U && src.depth() != CV_16U) {
            src.convertTo(src, CV_8U, 1.0 / 256.0);
        }

        // 3ch BGR → グレー
        if (src.channels() == 3) {
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
        }
        else if (src.channels() == 1) {
            gray = src;
        }
        else {
            // その他の特殊ケースはとりあえず 8bit グレーへ
            cv::Mat tmp;
            src.convertTo(tmp, CV_8U, 1.0 / 256.0);
            cv::cvtColor(tmp, gray, cv::COLOR_BGR2GRAY);
        }
    }

    // ImageProc::EdgeDetector を使ってエッジ検出を実行
    Polylines polylines;
    cv::Rect roi(0, 0, gray.cols, gray.rows);
    cv::Mat mask; // マスクなし

    int32_t ret = EdgeDetector::detect(
        gray,
        polylines,
        m_sigma,
        m_lowThre,
        m_highThre,
        roi,
        mask,
        m_dirMinDeg,
        m_dirMaxDeg
    );

    // つながっているエッジ同士を結合
    Polylines polylines2;
    EdgeDetector::ConnectEdges(polylines, polylines2, m_edgeConnectDist);

    // 一定長未満のエッジを除外
    Polylines polylines3;
    EdgeDetector::FilterLength(polylines2, polylines3, m_edgeMinLength);

    if (ret != 0) {
        // 失敗時は何も描画しない（m_edges は空のまま）
        return;
    }

    // 検出結果をメンバーに保存
    m_edges = std::move(polylines3);

    // 勾配強度の min/max を計算（ヒートマップ描画用）
    bool first = true;
    for (const auto& pl : m_edges) {
        for (const auto& v : pl) {
            double g = v.magnitude;
            if (first) {
                m_minMag = m_maxMag = g;
                first = false;
            } else {
                if (g < m_minMag) m_minMag = g;
                if (g > m_maxMag) m_maxMag = g;
            }
        }
    }
    if (first) {
        // エッジが一つもない場合の保険
        m_minMag = 0.0;
        m_maxMag = 1.0;
    }

    if (m_edgeColorMode == EdgeColorMode::Random) {
        ensurePolylineColors();
    }
}

// 解析結果の描画
void CannyEdgeAnalyzer::draw(
    QPainter& p,
    const QTransform& /*imageTransform*/,
    const QSize& /*imageSize*/)
{
    if (m_edges.empty())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // ------------------------------
    // Polyline ID カラー表示モード
    // ------------------------------
    if (m_edgeColorMode == EdgeColorMode::Random) {
        ensurePolylineColors();

        for (int i = 0; i < static_cast<int>(m_edges.size()); ++i) {
            const auto& poly = m_edges[i];
            if (poly.size() < 2)
                continue;

            QColor color = (i < m_polylineColors.size())
                ? m_polylineColors[i]
                : Qt::red;

            // 選択ポリラインだけ太い線で描画
            QPen pen(color);
            if (i == m_selectedPolyline)
                pen.setWidthF(1.0);       // 強調表示
            else
                pen.setWidthF(0.0);       // 1px 相当の最細線

            // ガタガタ抑制
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);

            p.setPen(pen);

            QVector<QPointF> qpts;
            qpts.reserve(poly.size());
            for (const auto& v : poly) {
                qpts.append(QPointF(v.pos.x, v.pos.y));
            }

            p.drawPolyline(qpts.constData(), qpts.size());
        }
    }
    // ------------------------------------------
    // 勾配強度に応じたヒートマップ描画モード
    // ------------------------------------------
    else {
        const double minVal = m_minMag;
        const double maxVal = (m_maxMag <= m_minMag) ? (m_minMag + 1.0) : m_maxMag;
        const double invRange = 1.0 / (maxVal - minVal);

        for (int i = 0; i < static_cast<int>(m_edges.size()); ++i) {
            const auto& poly = m_edges[i];
            if (poly.size() < 2)
                continue;

            // 各 segment ごとに描画
            for (size_t j = 1; j < poly.size(); ++j) {
                double g = poly[j].magnitude;
                double t = std::clamp((g - minVal) * invRange, 0.0, 1.0);

                int hue = static_cast<int>((1.0 - t) * 240.0); // 青→赤
                QColor color = QColor::fromHsv(hue, 255, 255);

                QPen pen(color);

                // 選択ポリラインだけ太く
                if (i == m_selectedPolyline)
                    pen.setWidthF(1.0);
                else
                    pen.setWidthF(0.0);

                // ガタガタ抑制
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);

                p.setPen(pen);

                QPointF p1(poly[j - 1].pos.x, poly[j - 1].pos.y);
                QPointF p2(poly[j].pos.x,     poly[j].pos.y);
                p.drawLine(p1, p2);
            }
        }
    }

    // ---- 矢印描画（選択点がある場合のみ） ------------------------------
    if (m_selectedPolyline >= 0 && m_selectedPoint >= 0) {

        const auto& poly = m_edges[m_selectedPolyline];
        const EdgeVertex& v = poly[m_selectedPoint];

        // 選択点の色と一致させる矢印色
        QColor arrowColor = Qt::yellow; // fallback

        if (m_edgeColorMode == EdgeColorMode::Random) {
            if (m_selectedPolyline < m_polylineColors.size())
                arrowColor = m_polylineColors[m_selectedPolyline];
        }
        else {
            // 勾配強度カラー
            double g = v.magnitude;
            double t = (g - m_minMag) /
                       ((m_maxMag > m_minMag) ? (m_maxMag - m_minMag) : 1.0);
            t = std::clamp(t, 0.0, 1.0);

            int hue = static_cast<int>((1.0 - t) * 240.0); // 青→赤
            arrowColor = QColor::fromHsv(hue, 255, 255);
        }

        // 勾配方向ベクトル（正規化済み角度）
        double dir = v.orientation;
        double vx = std::cos(dir);
        double vy = std::sin(dir);

        double arrowLen = 20.0;

        QPointF p0(v.pos.x,                v.pos.y);
        QPointF p1(v.pos.x + vx * arrowLen, v.pos.y + vy * arrowLen);

        // メイン線
        QPen pen(arrowColor, 1.0);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.drawLine(p0, p1);

        // 矢印の羽根
        double a = v.orientation + M_PI + M_PI / 6.0;
        double b = v.orientation + M_PI - M_PI / 6.0;

        QPointF p2(p1.x() + std::cos(a) * 6.0, p1.y() + std::sin(a) * 6.0);
        QPointF p3(p1.x() + std::cos(b) * 6.0, p1.y() + std::sin(b) * 6.0);

        p.drawLine(p1, p2);
        p.drawLine(p1, p3);
    }

    p.restore();
}

// --- property setter 実装 ---
// ここでは単純な clamp + 値の更新 + parametersChanged() emit をしている。
// 振る舞いは元コードと同じ。

void CannyEdgeAnalyzer::setSigma(double v)
{
    if (v <= 0.1) v = 0.1;
    if (m_sigma == v)
        return;
    m_sigma = v;
    emit parametersChanged();
}

void CannyEdgeAnalyzer::setLowThreshold(int32_t v)
{
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    if (m_lowThre == v)
        return;
    m_lowThre = v;
    emit parametersChanged();
}

void CannyEdgeAnalyzer::setHighThreshold(int32_t v)
{
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    if (m_highThre == v)
        return;
    m_highThre = v;
    emit parametersChanged();
}

void CannyEdgeAnalyzer::setEdgeColorMode(EdgeColorMode mode)
{
    if (m_edgeColorMode == mode)
        return;
    m_edgeColorMode = mode;
    emit parametersChanged();
}

// ポリライン数に応じて色を足していく
void CannyEdgeAnalyzer::ensurePolylineColors()
{
    // 8 色を繰り返し使う
    static const QVector<QColor> base = {
        QColor("#ff0000"),
        QColor("#00ff00"),
        QColor("#0000ff"),
        QColor("#00ffff"),
        QColor("#ff00ff"),
        QColor("#ffff00"),
        QColor("#ffffff"),
        QColor("#ff8000")
    };

    if (m_polylineColors.size() >= static_cast<int>(m_edges.size()))
        return;

    int idx    = m_polylineColors.size();
    int needed = static_cast<int>(m_edges.size()) - idx;
    for (int i = 0; i < needed; ++i) {
        m_polylineColors.push_back(base[(idx + i) % base.size()]);
    }
}

// 画像座標 imgPt に最も近いエッジ点を探す
int CannyEdgeAnalyzer::hitTestNearestPoint(const QPointF& imgPt)
{
    m_selectedPolyline = -1;
    m_selectedPoint    = -1;

    double bestDist = 1e9;

    for (int i = 0; i < static_cast<int>(m_edges.size()); ++i) {
        const auto& poly = m_edges[i];
        for (int j = 0; j < static_cast<int>(poly.size()); ++j) {

            double dx   = poly[j].pos.x - imgPt.x();
            double dy   = poly[j].pos.y - imgPt.y();
            double dist = std::sqrt(dx * dx + dy * dy);

            if (dist < bestDist) {
                bestDist          = dist;
                m_selectedPolyline = i;
                m_selectedPoint    = j;
            }
        }
    }

    // 3px以内だけヒット扱い（.NET版と同じルール）
    if (bestDist < 3.0)
        return m_selectedPolyline;

    m_selectedPolyline = -1;
    m_selectedPoint    = -1;
    return -1;
}
