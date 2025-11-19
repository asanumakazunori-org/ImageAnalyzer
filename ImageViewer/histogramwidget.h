// histogramwidget.h
#pragma once

#include <QWidget>
#include <array>

/*
 * HistogramWidget
 *
 * 表示中画像のヒストグラム（輝度 / RGB）を簡易表示するウィジェット。
 * setImage() に QImage と「カラー画像かどうか」のフラグを渡すと、
 * computeHistogram() でヒストグラムを計算し、paintEvent() で描画する。
 *
 * - m_hist[0] : 全体（輝度）
 * - m_hist[1] : R
 * - m_hist[2] : G
 * - m_hist[3] : B
 */
class HistogramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget *parent = nullptr);

    // 画像とその色情報をセット
    void setImage(const QImage& image, bool isColorImage);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void computeHistogram(const QImage &image);

    std::array<std::array<double, 256>, 4> m_hist{}; // 全体/R/G/B
    bool m_hasData  = false;
    bool m_hasColor = false;

    void drawHistSingle(
        QPainter& p,
        const QRect& rc,
        const std::array<double, 256>& hist,
        const QColor& color);
};
