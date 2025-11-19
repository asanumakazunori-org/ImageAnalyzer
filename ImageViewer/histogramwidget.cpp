// histogramwidget.cpp
#include "histogramwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <algorithm>

HistogramWidget::HistogramWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(150, 120);
}

void HistogramWidget::setImage(const QImage& image, bool isColorImage)
{
    if (image.isNull()) {
        clear();
        return;
    }

    computeHistogram(image);

    // 呼び出し側(MainWindow)で判定された色情報を信じる
    m_hasColor = isColorImage;

    m_hasData = true;
    update();
}

void HistogramWidget::clear()
{
    m_hasData = false;
    for (auto &ch : m_hist) ch.fill(0.0);
    update();
}

void HistogramWidget::computeHistogram(const QImage &src)
{
    for (auto &ch : m_hist) ch.fill(0.0);
    m_hasData  = false;
    m_hasColor = false;

    if (src.isNull()) return;

    QImage img = src;

    // グレースケール 8bit の場合
    if (img.format() == QImage::Format_Indexed8 ||
        img.format() == QImage::Format_Grayscale8)
    {
        m_hasColor = false;
        m_hasData  = true;

        const int w = img.width();
        const int h = img.height();

        for (int y = 0; y < h; ++y) {
            const uchar *line = img.constScanLine(y);
            for (int x = 0; x < w; ++x) {
                int v = line[x];          // 0〜255
                m_hist[0][v] += 1.0;      // 全体（輝度）ヒストグラム
            }
        }
    }
    else {
        // カラー → ARGB32 に統一
        if (img.format() != QImage::Format_ARGB32 &&
            img.format() != QImage::Format_RGB32)
        {
            img = img.convertToFormat(QImage::Format_ARGB32);
        }

        m_hasColor = true;
        m_hasData  = true;

        const int w = img.width();
        const int h = img.height();

        for (int y = 0; y < h; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb p = line[x];
                int r = qRed(p);
                int g = qGreen(p);
                int b = qBlue(p);
                int l = qGray(p);          // 全体の輝度 (0〜255)

                m_hist[0][l] += 1.0;       // 全体（輝度）
                m_hist[1][r] += 1.0;       // R
                m_hist[2][g] += 1.0;       // G
                m_hist[3][b] += 1.0;       // B
            }
        }
    }
}

void HistogramWidget::drawHistSingle(
    QPainter& p,
    const QRect& rc,
    const std::array<double, 256>& hist,
    const QColor& color)
{
    if (rc.height() <= 2 || rc.width() <= 2)
        return;

    // 背景
    p.fillRect(rc, Qt::black);

    // 正規化用の最大値
    double maxv = 0.0;
    for (double v : hist)
        maxv = std::max(maxv, v);
    if (maxv <= 0.0)
        return;

    // 左右マージンを少し取ると見栄えが良い
    const int margin = 3;  // 2〜5 くらいがおすすめ
    const int left   = rc.left()  + margin;
    const int right  = rc.right() - margin;
    const double width = (right - left);

    QPainterPath path;
    path.moveTo(left, rc.bottom());

    for (int i = 0; i < 256; ++i) {
        double ratio = hist[i] / maxv;  // 0〜1 に正規化
        double y     = rc.bottom() - ratio * rc.height();

        // x 座標もマージン付き領域に正規化して描画
        double x = left + width * (i / 255.0);

        path.lineTo(x, y);
    }

    path.lineTo(right, rc.bottom());
    path.closeSubpath();

    // 塗りつぶし
    QColor fillColor = color;
    fillColor.setAlpha(90);
    p.fillPath(path, fillColor);

    // 枠線
    QPen pen(color);
    pen.setWidth(1);
    pen.setCosmetic(true);
    p.strokePath(path, pen);
}

void HistogramWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    if (!m_hasData)
        return;

    int h = height() / (m_hasColor ? 3 : 1);
    int w = width();

    if (!m_hasColor) {
        // モノクロ → 白だけ
        drawHistSingle(p, QRect(0, 0, w, h), m_hist[0], Qt::white);
        return;
    }

    // カラー → RGB 各成分
    drawHistSingle(p, QRect(0,     0, w, h), m_hist[1], Qt::red);
    drawHistSingle(p, QRect(0,     h, w, h), m_hist[2], Qt::green);
    drawHistSingle(p, QRect(0, 2 * h, w, h), m_hist[3], Qt::blue);
}
