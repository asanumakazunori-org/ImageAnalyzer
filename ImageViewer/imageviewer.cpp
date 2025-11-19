// imageviewer.cpp
#include "imageviewer.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
#include <QClipboard>

#include <cmath>  // std::floor, std::log10, std::pow

#include "iaimageanalyzer.h"

ImageViewer::ImageViewer(QWidget* parent)
    : QWidget(parent)
{
    // ファイルドロップを受け付ける
    setAcceptDrops(true);
    // マウス移動イベントを常に受け取る（ボタンが押されていなくても）
    setMouseTracking(true);
    // キー入力（Ctrl+C）を受け取るためにフォーカスを取得可能にする
    setFocusPolicy(Qt::StrongFocus);
}

void ImageViewer::setImage(const QImage& image)
{
    m_image    = image;
    m_hasImage = !m_image.isNull();

    if (m_hasImage) {
        // 新しい画像が来たら等倍で中心に配置
        centerAt1x();
    } else {
        m_scale = 1.0;
        m_pan   = QPointF(0, 0);
    }
    update();
}

void ImageViewer::setAnalyzer(IaImageAnalyzer* analyzer)
{
    m_analyzer = analyzer;
    update();
}

void ImageViewer::clearAnalyzer()
{
    m_analyzer = nullptr;
    update();
}

// 画像座標 → デバイス座標の変換行列を作成
QTransform ImageViewer::imageTransform() const
{
    QTransform tr;
    // 上と左にルーラーがあるので、その分だけオフセット
    tr.translate(m_rulerThickness + m_pan.x(), m_rulerThickness + m_pan.y());
    tr.scale(m_scale, m_scale);
    return tr;
}

// 等倍で画像中心をビュー中心に配置
void ImageViewer::centerAt1x()
{
    if (!m_hasImage)
        return;

    m_scale = 1.0;

    // ビューの中心（デバイス座標）
    QPointF centerDev = rect().center();

    // 画像の中心（画像座標：ピクセル中心を考慮して -1/2）
    double cx = (m_image.width()  - 1) * 0.5;
    double cy = (m_image.height() - 1) * 0.5;

    // transform: T(x) = (ruler + pan) + scale * x
    m_pan.setX(centerDev.x() - m_rulerThickness - m_scale * cx);
    m_pan.setY(centerDev.y() - m_rulerThickness - m_scale * cy);

    update();
}

void ImageViewer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 背景（チェッカーフラッグ）
    drawBackground(p);

    if (m_hasImage) {
        // 画像座標系 → ウィジェット座標系への変換
        QTransform tr = imageTransform();

        // 画像と解析結果は同じ transform で描画
        p.setWorldTransform(tr);
        drawImage(p);
        drawAnalyzerOverlay(p);

        // ルーラーはウィジェット座標系（transform なし）で描く
        p.setWorldTransform(QTransform());
        drawRulers(p);
    }
    else {
        // 画像がないときはルーラーだけ
        drawRulers(p);
    }
}

void ImageViewer::drawBackground(QPainter& p)
{
    p.save();

    // チェッカーフラッグパターン生成
    const int tile = 6;  // マスの大きさ（見やすいサイズ）
    QPixmap checker(tile * 2, tile * 2);
    checker.fill(Qt::lightGray);

    QPainter pt(&checker);
    pt.fillRect(0,     0,     tile, tile, QColor(220, 220, 220)); // 左上
    pt.fillRect(tile,  tile,  tile, tile, QColor(220, 220, 220)); // 右下
    pt.end();

    // タイルブラシで背景塗りつぶし
    p.fillRect(rect(), QBrush(checker));

    p.restore();
}

void ImageViewer::drawImage(QPainter& p)
{
    if (m_image.isNull())
        return;

    // 画像座標系の原点を
    //   「画像左上ピクセルの中心 = (0,0)」
    // にしたい。
    //
    // Qt の QImage は (0,0) が「左上ピクセルの左上角」なので、
    // ピクセル中心とのズレが (0.5, 0.5) だけある。
    //
    // そこで、画像の左上角を (-0.5, -0.5) に配置することで、
    // ピクセル中心 (0.0, 0.0) がワールド座標 (0,0) に一致するようにする。
    const QPointF topLeft(-0.5, -0.5);
    p.drawImage(topLeft, m_image);
}

void ImageViewer::drawAnalyzerOverlay(QPainter& p)
{
    if (!m_hasImage || !m_analyzer)
        return;

    // paintEvent 側で既に imageTransform() が p に設定されている前提。
    // ここでは transform を変更せず、そのまま画像座標系で描画する。
    m_analyzer->draw(p, imageTransform(), m_image.size());
}

void ImageViewer::drawRulers(QPainter& p)
{
    if (!m_hasImage)
        return;

    drawRulerX(p);
    drawRulerY(p);
}

// ----------------- マウス・ホイール操作 -----------------

void ImageViewer::wheelEvent(QWheelEvent* event)
{
    if (!m_hasImage) {
        event->ignore();
        return;
    }

    // ホイール量（通常 120 単位）
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }

    // ズームセンター（デバイス座標）：マウス位置
    QPointF mousePos = event->position();

    double oldScale = m_scale;
    double factor   = (delta > 0) ? 1.2 : (1.0 / 1.2);

    m_scale *= factor;
    if (m_scale < 0.05) m_scale = 0.05;
    if (m_scale > 50.0) m_scale = 50.0;

    // 画像座標系での「マウス位置」を保つように pan を調整
    QPointF offset(m_rulerThickness + m_pan.x(),
                   m_rulerThickness + m_pan.y());
    QPointF imgPos = (mousePos - offset) / oldScale; // 画像座標

    m_pan.setX(mousePos.x() - m_rulerThickness - imgPos.x() * m_scale);
    m_pan.setY(mousePos.y() - m_rulerThickness - imgPos.y() * m_scale);

    update();
}

void ImageViewer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning      = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }

    QWidget::mousePressEvent(event);
}

void ImageViewer::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();

        m_pan += QPointF(delta);
        update();
    }

    if (m_analyzer) {
        // viewer → image 変換
        QTransform inv = imageTransform().inverted();
        QPointF imgPt  = inv.map(event->pos());

        // Analyzer へヒットテストを依頼
        int hitId = m_analyzer->hitTestNearestPoint(imgPt);

        if (hitId >= 0)
            update();  // 再描画（太い線になるなど）
        else
            update();
    }

    QWidget::mouseMoveEvent(event);
}

void ImageViewer::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        unsetCursor();
    }

    QWidget::mouseReleaseEvent(event);
}

// 左ダブルクリックで等倍センタリング
void ImageViewer::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_hasImage) {
        centerAt1x();
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

// ----------------- ドラッグ＆ドロップ -----------------

void ImageViewer::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
    else {
        QWidget::dragEnterEvent(event);
    }
}

void ImageViewer::dropEvent(QDropEvent* event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        event->ignore();
        return;
    }

    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) {
        event->ignore();
        return;
    }

    emit imageDropped(path);
    event->acceptProposedAction();
}

// ----------------- ルーラー描画 -----------------
//
//   getNearestPitchPix(): 画像空間での目盛り間隔（ピクセル）を「見やすい値」に丸める。
//   getSubSplitNum():      補助目盛りの数を決める。

static int getNearestPitchPix(float pitch)
{
    // 10, 20, 25, 50, 100... のような「きれいな間隔」を返す
    const int steps[] = { 1, 2, 5 };
    int   exp10 = static_cast<int>(std::floor(std::log10(pitch)));
    float base  = std::pow(10.0f, exp10);
    for (int s : steps) {
        float val = s * base;
        if (val >= pitch)
            return static_cast<int>(val);
    }
    return static_cast<int>(10 * base);
}

static int getSubSplitNum(float text_pitch_disp)
{
    if (text_pitch_disp < 20) return 2;
    if (text_pitch_disp < 50) return 5;
    return 10;
}

void ImageViewer::drawRulerX(QPainter& p)
{
    if (!m_hasImage) return;

    p.save();
    p.setWorldTransform(QTransform());  // ruler はデバイス座標で描く

    const int rulerH = m_rulerThickness;
    const int w      = width();

    // 背景
    p.fillRect(0, 0, w, rulerH, QColor(200, 230, 255, 200));
    p.setPen(Qt::black);
    p.drawText(2, rulerH - 4, "pix");

    if (m_scale <= 0.0) {
        p.restore();
        return;
    }

    // 画像座標 → デバイス座標の変換行列
    QTransform dispMatrix = imageTransform();

    // デバイス上での理想的な表示間隔（px）
    float text_pitch_disp = 50.0f;

    // 画像空間での間隔（ピクセル）
    float text_pitch_pix  = text_pitch_disp / dispMatrix.m11();
    int   text_pitch_pixd = getNearestPitchPix(text_pitch_pix);

    // 実際のデバイス間隔（px）
    text_pitch_disp = text_pitch_pixd * dispMatrix.m11();

    int imgW = m_image.width();

    for (int pix = 0; pix <= imgW; pix += text_pitch_pixd) {
        float dsp = static_cast<float>(dispMatrix.dx() + dispMatrix.m11() * pix);
        if (dsp < 0.0f || dsp > w) continue;

        // 主目盛（80%）
        p.drawLine(dsp, 0, dsp, rulerH * 0.8f);
        p.drawText(dsp + 2, rulerH - 2, QString::number(pix));
    }

    // 補助目盛
    int split_num = getSubSplitNum(text_pitch_disp);
    int sub_pitch = text_pitch_pixd / split_num;
    if (sub_pitch <= 0) sub_pitch = 1;

    for (int pix = 0; pix <= imgW; pix += sub_pitch) {
        float dsp = static_cast<float>(dispMatrix.dx() + dispMatrix.m11() * pix);
        if (dsp < 0.0f || dsp > w) continue;

        float h = rulerH * 0.2f;
        if (pix % (sub_pitch * 5) == 0)
            h = rulerH * 0.4f;

        p.drawLine(dsp, 0, dsp, h);
    }

    p.restore();
}

void ImageViewer::drawRulerY(QPainter& p)
{
    if (!m_hasImage) return;

    p.save();
    p.setWorldTransform(QTransform());

    const int rulerW = m_rulerThickness;
    const int h      = height();

    // 背景
    p.fillRect(0, rulerW, rulerW, h - rulerW, QColor(200, 230, 255, 200));
    p.setPen(Qt::black);
    p.drawText(2, rulerW - 4, "pix");

    if (m_scale <= 0.0) {
        p.restore();
        return;
    }

    QTransform dispMatrix = imageTransform();

    float text_pitch_disp = 50.0f;
    float text_pitch_pix  = text_pitch_disp / dispMatrix.m22();
    int   text_pitch_pixd = getNearestPitchPix(text_pitch_pix);
    text_pitch_disp       = text_pitch_pixd * dispMatrix.m22();

    int imgH = m_image.height();

    for (int pix = 0; pix <= imgH; pix += text_pitch_pixd) {
        float dsp = static_cast<float>(dispMatrix.dy() + dispMatrix.m22() * pix);
        if (dsp < rulerW || dsp > h) continue;

        // 主目盛（80%）
        p.drawLine(rulerW, dsp, rulerW - rulerW * 0.8f, dsp);
        p.drawText(2, dsp - 2, QString::number(pix));
    }

    // 補助目盛
    int split_num = getSubSplitNum(text_pitch_disp);
    int sub_pitch = text_pitch_pixd / split_num;
    if (sub_pitch <= 0) sub_pitch = 1;

    for (int pix = 0; pix <= imgH; pix += sub_pitch) {
        float dsp = static_cast<float>(dispMatrix.dy() + dispMatrix.m22() * pix);
        if (dsp < rulerW || dsp > h) continue;

        float w = rulerW * 0.2f;
        if (pix % (sub_pitch * 5) == 0)
            w = rulerW * 0.4f;

        p.drawLine(rulerW, dsp, rulerW - w, dsp);
    }

    p.restore();
}

void ImageViewer::copyImageOnlyToClipboard()
{
    if (m_image.isNull())
        return;

    // 画像サイズそのまま
    QImage out(m_image.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);

    QPainter p(&out);

    // --- 1. 元画像を等倍描画 ---
    p.drawImage(QPoint(0, 0), m_image);

    // --- 2. エッジ（Analyzer Overlay）を描画 ---
    if (m_analyzer) {
        QTransform identity;
        // ※ コピーでは等倍描画のため Identity に固定
        m_analyzer->draw(
            p,
            identity,         // imageTransform の代わり（等倍描画）
            m_image.size()
        );
    }

    // --- 3. クリップボードへ ---
    QApplication::clipboard()->setImage(out);
}

void ImageViewer::keyPressEvent(QKeyEvent* e)
{
    // Ctrl + C のときに画像＋オーバーレイをコピー
    if (e->modifiers() == Qt::ControlModifier && e->key() == Qt::Key_C) {
        copyImageOnlyToClipboard();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}
