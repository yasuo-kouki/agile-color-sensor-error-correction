# leatset_models

## プロジェクト概要
Arduino UNO R4 WiFi に接続した RGB センサで取得した値をニューラルネットワークで補正し、基準画像に近い色を再現するための学習済みモデル群とファームウェア一式です。`leatset_models` ディレクトリでは、以下の最適化を段階的に適用しながら、推論をマイコン向けに圧縮しています。

- ベースライン（32bit float）/ `model_nomal`
- スパース化のみ / `model_sparse`
- CSR（Compressed Sparse Row）表現 / `model_csr`
- int8 量子化のみ / `model_quantize`
- スパース + CSR + int8 を組み合わせたモデル / `model_sparse_csr_int8`

入力は 3ch の RGB、出力は補正済み RGB で、隠れ層 2 層（30〜120 ユニット）・ReLU 構成の小型 MLP を採用しています。

## データセットと学習
- 参照画像: `leatset_models/img/reference_image{1,2}_cmyk_large.png`
  - `reference_image1` を 3×3、`reference_image2` を 6×4 に最近傍補間で縮小し、27 + 72 = 99 個の RGB サンプルを教師データとして使用します。
- 学習スクリプト: `leatset_models/Csr_int8_model.ipynb`
  - PyTorch (2 層 MLP, Adam lr=0.01, epoch=50k, MSE+L1) で複数のパラメータ数（30〜1100）を一括学習。
  - 学習後に各重みを C++ 配列へ変換し、`h_faile_data/` 以下に `.h` ヘッダを自動生成。
  - ノートブック内で CSR 化・スパース率 sweeping・int8 量子化パラメータの計算まで実施。

### 再現手順
1. Python 3.11 + Poetry/venv 等で以下ライブラリをインストール: `torch`, `numpy`, `opencv-python`, `matplotlib`.
2. `leatset_models/Csr_int8_model.ipynb` を実行し、学習完了後に `model_*` ディレクトリへ必要なヘッダをコピー。
3. スパース率や隠れ層幅を変更したい場合は、`PARAMETER_list` とスパース率設定を編集し再実行。

## ファームウェア構成
- RGB 計測・ボタン操作などの周辺処理は全モデル共通です。予測ロジックのみがヘッダの構造に合わせて分岐します。
- 例: CSR + int8 推論（`model_sparse_csr_int8/model_sparse_csr_int8.ino`）

```1:68:leatset_models/model_sparse_csr_int8/model_sparse_csr_int8.ino
#include <Arduino.h>
#include "model_sparse0.005_csr_int8_param60.h"
#define INPUT_SIZE 3
#define HIDDEN_SIZE1 60
#define HIDDEN_SIZE2 60
#define OUTPUT_SIZE 3
float relu(float x) { return (x > 0) ? x : 0; }
void predict(float input[INPUT_SIZE], float output[OUTPUT_SIZE]) {
    float layer1[HIDDEN_SIZE1];
    float layer2[HIDDEN_SIZE2];
    for (int i = 0; i < HIDDEN_SIZE1; ++i) {
        float sum = ((float)bias_1[i]) * bias_1_scale;
        for (int idx = weight_1_indptr[i]; idx < weight_1_indptr[i + 1]; ++idx) {
            int j = weight_1_indices[idx];
            sum += ((float)weight_1_data[idx]) * weight_1_scale * input[j];
        }
        layer1[i] = relu(sum);
    }
    // Layer2/3 も同様に CSR + int8 を復元しながら演算
}
```

### モデル別の `.ino` とヘッダ
| ディレクトリ | 目的 | 主なヘッダ |
| --- | --- | --- |
| `model_nomal/` | float32 フルモデル | `nomal_model_parameters_*.h` |
| `model_sparse/` | L1 で刈り取ったスパース重み | `model_parameters_sparse_only.h` |
| `model_csr/` | スパース重みを CSR 形式に変換 | `model_parameters_csr_only.h` |
| `model_quantize/` | 全結合層を int8 量子化 | `model_parameters_quantize_only.h` |
| `model_sparse_csr_int8/` | スパース + CSR + int8 + 可変ユニット数 | `model_sparse0.005_csr_int8_param{30,40,...}.h` |
| `model_sparse_csr_int8_0.01_compare/` | スパース率 0.01 の比較用 | `model_sparse0.01_csr_int8_param*.h` |
| `sparse_ratetest_csr_int8_param*/` | 各スパース率・ユニット数の評価スケッチ | 同名ヘッダ |

`models/` および `models_1/` には Arduino 以外の環境で流用しやすい共通ヘッダをまとめています。

## Arduino への書き込み手順
1. Arduino IDE で対象 `.ino` を開き、デバイスを UNO R4 WiFi に設定。
2. 必要なヘッダを同ディレクトリにコピーし、`#include` のファイル名を合わせる（例: `model_sparse0.005_csr_int8_param60.h`）。
3. シリアルモニタでセンサ読み取り行数（Height）を入力 → ボタン操作で画素を順番に採取 → 取得完了後に PPM 形式がシリアルへ出力される。
4. 予測時間は起動後 5 回分が `Predict time: xxx microseconds` として出力され、最適化前後の比較に利用できます。

## 実験ログ
- `data/` 配下: さまざまなパラメータ数やスパース率で生成した画像例・PPM ファイル。
- `spars_rate/`, `sparse_model/`: スパース率のスイープ結果とそれぞれのモデルパラメータ。
- `data/model_sparse0.005_csr_int8_param*/`: 実機で取得した png/ppm、スクリーンショット。

## ライセンス
リポジトリ全体のライセンスを設定していないため、公開時は用途に応じて `LICENSE` ファイルを追加してください。# agile-color-sensor-error-correction
