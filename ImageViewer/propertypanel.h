// propertypanel.h
#pragma once

#include <QWidget>
#include <QVector>
#include <QMetaProperty>

class QFormLayout;
class QObject;

/*
 * PropertyPanel
 *
 * 解析オブジェクト（Canny/Labelingなど）のパラメータを、
 * Qt のメタオブジェクト機構（Q_PROPERTY）経由で自動表示する
 * 簡易プロパティパネル。
 *
 * 【使い方 / 新しい Analyzer でパラメータを追加する手順】
 *   - Analyzer クラス（IaImageAnalyzer を継承）のヘッダで:
 *       Q_PROPERTY(int threshold READ threshold WRITE setThreshold NOTIFY parametersChanged)
 *     のような Q_PROPERTY を定義しておく。
 *   - parametersObject() で通常 this を返す。
 *   - MainWindow が propertyPanel->setTarget(analyzer->parametersObject())
 *     を呼ぶと、このパネルが metaObject を調べて自動的に UI を構築する。
 *   - __category_xxx という QString Q_PROPERTY を定義すると、
 *     その名前のカテゴリ見出しが PropertyPanel 上に表示される。
 */
class PropertyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertyPanel(QWidget* parent = nullptr);

    // パラメータを編集する対象（QObjectであれば何でも可）
    void setTarget(QObject* target);

signals:
    // どれかのプロパティが変更されたときに発行
    void propertyChanged();

private:
    struct EditorInfo {
        QMetaProperty property; // 対応するプロパティ情報（メタ情報）
        QWidget* editor;        // 編集用ウィジェット（SpinBox, CheckBox, ComboBox など）
    };

    void clearEditors(); // 既存エディタの破棄
    void rebuild();      // m_target からプロパティ一覧を作り直す

    // 各種エディタ用の更新スロット
    void connectSpinBox(QObject* editorObj, int index);
    void connectDoubleSpinBox(QObject* editorObj, int index);
    void connectCheckBox(QObject* editorObj, int index);
    void connectComboBox(QObject* editorObj, int index);   // enum 用

private:
    QObject*     m_target      = nullptr;   // 解析オブジェクト（Canny/Labelingなど）
    QFormLayout* m_formLayout  = nullptr;
    QVector<EditorInfo> m_editors;         // 作成したエディタ情報
};
