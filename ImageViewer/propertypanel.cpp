// propertypanel.cpp
#include "propertypanel.h"

#include <QFormLayout>
#include <QLabel>
#include <QMetaObject>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>

PropertyPanel::PropertyPanel(QWidget* parent)
    : QWidget(parent)
    , m_formLayout(new QFormLayout(this))
{
    m_formLayout->setContentsMargins(4, 4, 4, 4);
    m_formLayout->setSpacing(4);

    // 初期状態メッセージ
    m_formLayout->addRow(new QLabel(tr("No parameters")));
}

void PropertyPanel::setTarget(QObject* target)
{
    if (m_target == target)
        return;

    m_target = target;
    rebuild();
}

void PropertyPanel::clearEditors()
{
    m_editors.clear();

    if (!m_formLayout)
        return;

    // レイアウトから項目をすべて取り外し、ウィジェットを破棄
    while (QLayoutItem* item = m_formLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
}

void PropertyPanel::rebuild()
{
    clearEditors();

    if (!m_target) {
        m_formLayout->addRow(new QLabel(tr("No parameters")));
        return;
    }

    const QMetaObject* mo = m_target->metaObject();
    if (!mo) {
        m_formLayout->addRow(new QLabel(tr("No meta object")));
        return;
    }

    // QObject に定義されているプロパティをスキップするためのオフセット
    int offset = QObject::staticMetaObject.propertyCount();
    int count  = mo->propertyCount();

    for (int i = offset; i < count; ++i) {
        QMetaProperty prop = mo->property(i);
        if (!prop.isValid())
            continue;
        if (!prop.isWritable())
            continue;

        const char* name = prop.name();
        QString nameStr  = QString::fromLatin1(name);

        // ============================
        // ① カテゴリ名プロパティの処理
        //    __category_xxx という名前なら
        //    見出しラベルだけ追加して continue
        // ============================
        if (nameStr.startsWith("__category_")) {
            QString cat = nameStr.mid(QString("__category_").length());
            // 太字ラベルでカテゴリ見出し
            auto* title = new QLabel(QStringLiteral("<b>%1</b>").arg(cat), this);
            m_formLayout->addRow(title);
            continue; // エディタは作らない
        }

        // ここから通常のプロパティ編集用ウィジェット
        QVariant value = m_target->property(name);
        QWidget* editor = nullptr;

        // 表示名（ラベルに出すテキスト）を決める
        QString labelText = nameStr;
        if (nameStr == "lowThreshold") {
            labelText = "lowThre";
        }
        else if (nameStr == "highThreshold") {
            labelText = "highThre";
        }
        // sigma はそのまま "sigma" なので特別扱い不要

        // ---------- enum プロパティ ----------
        if (prop.isEnumType()) {
            QMetaEnum metaEnum = prop.enumerator();

            auto* combo = new QComboBox(this);
            for (int k = 0; k < metaEnum.keyCount(); ++k) {
                combo->addItem(metaEnum.key(k), metaEnum.value(k));
            }

            combo->setCurrentIndex(combo->findData(value.toInt()));
            editor = combo;

            EditorInfo info;
            info.property = prop;
            info.editor   = combo;

            int index = m_editors.size();
            m_editors.push_back(info);

            m_formLayout->addRow(labelText, combo);
            connectComboBox(combo, index);
            continue;
        }

        // ---------- double ----------
        if (prop.type() == QMetaType::Double || prop.type() == QMetaType::Float) {
            auto* spin = new QDoubleSpinBox(this);

            if (nameStr == "sigma") {
                spin->setDecimals(1);          // 小数1桁
                spin->setSingleStep(0.1);      // 0.1刻み
                spin->setRange(0.0, 10.0);     // 0.0 ～ 10.0

                double v = value.toDouble();   // clamp
                if (v < 0.0) v = 0.0;
                if (v > 10.0) v = 10.0;
                spin->setValue(v);
            }
            else {
                // デフォルト設定（他の double プロパティ）
                spin->setDecimals(4);
                spin->setSingleStep(0.1);
                spin->setRange(-1e9, 1e9);
                spin->setValue(value.toDouble());
            }

            editor = spin;

            EditorInfo info{ prop, spin };
            int index = m_editors.size();
            m_editors.push_back(info);
            m_formLayout->addRow(labelText, spin);

            connectDoubleSpinBox(spin, index);
            continue;
        }

        // ---------- int ----------
        if (prop.type() == QMetaType::Int) {
            auto* spin = new QSpinBox(this);

            if (nameStr == "lowThreshold" || nameStr == "highThreshold") {
                spin->setRange(0, 255);    // 0 ～ 255
                int v = value.toInt();     // clamp
                if (v < 0)   v = 0;
                if (v > 255) v = 255;
                spin->setValue(v);
            }
            else if (nameStr == "dirMinDeg" || nameStr == "dirMaxDeg") {
                spin->setRange(-180, 180);
                int v = value.toInt();
                if (v < -180) v = -180;
                if (v > 180)  v = 180;
                spin->setValue(v);
            }
            else if (nameStr == "edgeConnectDist") {
                spin->setRange(0, 10);
                int v = value.toInt();
                if (v < 0)  v = 0;
                if (v > 10) v = 10;
                spin->setValue(v);
            }
            else if (nameStr == "edgeMinLength") {
                spin->setRange(0, 1000);
                int v = value.toInt();
                if (v < 0)     v = 0;
                if (v > 1000)  v = 10;
                spin->setValue(v);
            }
            else {
                // デフォルト
                spin->setRange(-1000000, 1000000);
                spin->setValue(value.toInt());
            }

            editor = spin;

            EditorInfo info{ prop, spin };
            int index = m_editors.size();
            m_editors.push_back(info);
            m_formLayout->addRow(labelText, spin);

            connectSpinBox(spin, index);
            continue;
        }

        // ---------- bool ----------
        if (prop.type() == QMetaType::Bool) {
            auto* chk = new QCheckBox(this);
            chk->setChecked(value.toBool());
            editor = chk;

            EditorInfo info;
            info.property = prop;
            info.editor   = chk;

            int index = m_editors.size();
            m_editors.push_back(info);

            m_formLayout->addRow(labelText, chk);
            connectCheckBox(chk, index);
            continue;
        }
    }

    if (m_editors.isEmpty()) {
        m_formLayout->addRow(new QLabel(tr("No editable parameters")));
    }
}

void PropertyPanel::connectSpinBox(QObject* editorObj, int index)
{
    auto* spin = qobject_cast<QSpinBox*>(editorObj);
    if (!spin) return;

    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, index](int v) {
                if (!m_target || index < 0 || index >= m_editors.size())
                    return;

                auto& info = m_editors[index];
                info.property.write(m_target, v);
                emit propertyChanged();
            });
}

void PropertyPanel::connectDoubleSpinBox(QObject* editorObj, int index)
{
    auto* spin = qobject_cast<QDoubleSpinBox*>(editorObj);
    if (!spin) return;

    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, index](double v) {
                if (!m_target || index < 0 || index >= m_editors.size())
                    return;

                auto& info = m_editors[index];
                info.property.write(m_target, v);
                emit propertyChanged();
            });
}

void PropertyPanel::connectCheckBox(QObject* editorObj, int index)
{
    auto* chk = qobject_cast<QCheckBox*>(editorObj);
    if (!chk) return;

    connect(chk, &QCheckBox::toggled,
            this, [this, index](bool v) {
                if (!m_target || index < 0 || index >= m_editors.size())
                    return;

                auto& info = m_editors[index];
                info.property.write(m_target, v);
                emit propertyChanged();
            });
}

void PropertyPanel::connectComboBox(QObject* editorObj, int index)
{
    QComboBox* combo = qobject_cast<QComboBox*>(editorObj);
    if (!combo) return;

    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, index](int /*unused*/) {
                if (!m_target || index < 0 || index >= m_editors.size())
                    return;

                auto& info = m_editors[index];
                QComboBox* cb = qobject_cast<QComboBox*>(info.editor);
                if (!cb) return;

                QVariant v = cb->currentData();
                info.property.write(m_target, v);
                emit propertyChanged();
            });
}
