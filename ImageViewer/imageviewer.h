// imageviewer.h
#pragma once

#include <QWidget>
#include <QImage>
#include <QPointF>

class IaImageAnalyzer;

/*
 * ImageViewer
 *
 * 画像の表示を担当するビュークラス。
 * - QImage（24bit カラー）を保持し、ズーム・パン付きで描画する。
 * - IaImageAnalyzer を 1 つ保持し、その draw() を使ってオーバーレイ描画する。
 * - マウスホイールでズーム、ドラッグでパン、Ctrl+C で画像＋オーバーレイをクリップボードへコピー。
 * - 画像の上と左に「ピクセル座標のルーラー」を描画する。
 *
 * 【座標系の決め方】
 *   - 内部的には「画像左上ピクセルの中心 = (0,0)」となるように
 *     わずかに座標をずらして描画している（drawImage() 参照）。
 *   - Analyzer 側も、原則としてこの「ピクセル中心座標」を前提として描画する。
 */
class ImageViewer : public QWidget
{
    Q_OBJECT
public:
    explicit ImageViewer(QWidget* parent = nullptr);

    // 表示する画像をセット（24bitカラー想定）
    void setImage(const QImage& image);
    bool hasImage() const { return m_hasImage; }
    QSize imageSize() const { return m_image.size(); }

    // 解析オブジェクトをセット（描画オーバーレイ用）
    //   ImageViewer は IaImageAnalyzer を所有しない（生ポインタ保持）。
    void setAnalyzer(IaImageAnalyzer* analyzer);
    void clearAnalyzer();

    // 等倍（1.0倍）で画像中心をビュー中心に配置
    void centerAt1x();

signals:
    // ビューに画像ファイルがドロップされたときに通知
    void imageDropped(const QString& filePath);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    // 画像座標 → ウィジェット座標の変換行列（ズーム・パン＋ルーラーオフセット）
    QTransform imageTransform() const;

    // 描画の細かい処理
    void drawBackground(QPainter& p);
    void drawImage(QPainter& p);
    void drawAnalyzerOverlay(QPainter& p);
    void drawRulers(QPainter& p);

    // ルーラー描画（インスタンスの m_scale / m_pan を使用）
    void drawRulerX(QPainter& p);
    void drawRulerY(QPainter& p);

    // Ctrl+C で「画像＋解析オーバーレイ」をクリップボードにコピー
    void copyImageOnlyToClipboard();

private:
    QImage   m_image;
    bool     m_hasImage = false;

    double   m_scale = 1.0;        // ズーム倍率
    QPointF  m_pan   = { 0.0, 0.0 }; // 画像描画位置（左上座標オフセット）
    QPoint   m_lastMousePos;
    bool     m_panning = false;

    int      m_rulerThickness = 24; // 上・左のルーラー幅（ピクセル）

    IaImageAnalyzer* m_analyzer = nullptr; // 解析オーバーレイ用（所有権なし）
};
