#include <Arduino.h>
#include "model_sparse0.005_csr_int8_param1000.h"
#define INPUT_SIZE 3
#define HIDDEN_SIZE1 1000
#define HIDDEN_SIZE2 1000
#define OUTPUT_SIZE 3


// ReLU関数
float relu(float x) { return (x > 0) ? x : 0; }



/*
  量子化済みパラメータをオンザフライで float に復元しつつ演算します，
// 想定：ヘッダに以下が定義されていること
// weight_1_data[], weight_1_indices[], weight_1_indptr[], weight_1_shape[2], weight_1_scale
// bias_1[] , bias_1_scale
// 同様に weight_2_*, bias_2_, weight_3_, bias_3_ があること．
*/
void predict(float input[INPUT_SIZE], float output[OUTPUT_SIZE]) {
    // 層出力のバッファ
    float layer1[HIDDEN_SIZE1];
    float layer2[HIDDEN_SIZE2];

    // --- Layer 1 : input -> hidden1 ---
    for (int i = 0; i < HIDDEN_SIZE1; ++i) {
        // bias を復元して初期値にする．
        // bias_1 は int8_t 配列，bias_1_scale は float スケールとする前提．
        float sum = ((float)bias_1[i]) * bias_1_scale;      

        int start = weight_1_indptr[i];
        int end   = weight_1_indptr[i + 1];
        // CSRの非ゼロを巡回して加算する．
        for (int idx = start; idx < end; ++idx) {
            int j = weight_1_indices[idx];            // 入力インデックス
            // weight_1_data[idx] は int8，weight_1_scale を掛けて float に復元して乗算する．
            sum += ((float)weight_1_data[idx]) * weight_1_scale * input[j];
        }
        layer1[i] = relu(sum);
    }

    // --- Layer 2 : hidden1 -> hidden2 ---
    for (int i = 0; i < HIDDEN_SIZE2; ++i) {
        float sum = ((float)bias_2[i]) * bias_2_scale;

        int start = weight_2_indptr[i];
        int end   = weight_2_indptr[i + 1];
        for (int idx = start; idx < end; ++idx) {
            int j = weight_2_indices[idx];            // 前層ノードインデックス
            sum += ((float)weight_2_data[idx]) * weight_2_scale * layer1[j];
        }
        layer2[i] = relu(sum);
    }

    // --- Output Layer : hidden2 -> output ---
    for (int i = 0; i < OUTPUT_SIZE; ++i) {
        float sum = ((float)bias_3[i]) * bias_3_scale;

        int start = weight_3_indptr[i];
        int end   = weight_3_indptr[i + 1];
        for (int idx = start; idx < end; ++idx) {
            int j = weight_3_indices[idx];
            sum += ((float)weight_3_data[idx]) * weight_3_scale * layer2[j];
        }
        output[i] = sum; // 出力は線形そのまま
    }
}



// RGB点灯遅延
#define RgbFlashDelay 10

// ピン割り当て
#define PIN_IN A0
#define led_r_pin 8
#define led_g_pin 6
#define led_b_pin 7
#define buttonInputPin 3
#define buttonMinMaxPin 2

// ボタン押下を検知する変数 (余計な最適化を防ぐため、volatile宣言をしている)
volatile bool buttonInputPressed = false;
volatile bool buttonMinMaxPressed = false;

// RGB読み取り用変数
float r=0, g=0, b=0;
float r_max=512, g_max=512, b_max=512;
float r_min=0, g_min=0, b_min=0;

float rgb[3]{0, 0, 0};
float RGBInput[3]{0, 0, 0};
float RGBOutput[3]{0, 0, 0};

// RGBデータメモリ
int N, M;
float **array;
int n=0, m=0;

int pushed = 0;
int predict_time_count = 0;  // predict関数の処理時間出力カウンター

void buttonInput() {
    buttonInputPressed = true;
}


void buttonMinMax() {
    buttonMinMaxPressed = true;
}


float **allocateArray(int N, int M) {
    float **array = (float **)malloc(N * sizeof(float *)); // 行ポインタの確保
    if (array == NULL) {
        Serial.println("メモリ確保失敗");
        return NULL;
    }

    for (int i = 0; i < N; i++) {
        array[i] = (float *)malloc(M * sizeof(float)); // 各行のメモリ確保
        if (array[i] == NULL) {
            Serial.println("メモリ確保失敗（部分的）");
            // 確保済みのメモリを解放して終了
            for (int j = 0; j < i; j++) {
                free(array[j]);
            }
            free(array);
            return NULL;
        }
    }
    return array;
}

void read(){
    for(int count = 0 ; count <  3; count++){
        if(count == 0){
            analogWrite(led_r_pin, 255);
            analogWrite(led_g_pin,   0);
            analogWrite(led_b_pin,   0);
            delay(RgbFlashDelay);
            rgb[0] = analogRead(PIN_IN);
        }else if(count == 1){
            analogWrite(led_r_pin,   0);
            analogWrite(led_g_pin, 255); 
            analogWrite(led_b_pin,   0);
            delay(RgbFlashDelay);
            rgb[1] = analogRead(PIN_IN);
        }else{
            analogWrite(led_r_pin,   0);
            analogWrite(led_g_pin,   0);
            analogWrite(led_b_pin, 255);
            delay(RgbFlashDelay);     
            rgb[2] = analogRead(PIN_IN);
        }
    }
    analogWrite(led_r_pin, 0);
    analogWrite(led_g_pin, 0);
    analogWrite(led_b_pin, 0);
}


// Arduino's analog input detects 0-5V with 10-bit values
// https://deviceplus.jp/arduino/arduino_f07/
void readAndProcess(){
    read();
    RGBInput[0] = (float(rgb[0])-r_min)/r_max;
    RGBInput[1] = (float(rgb[1])-g_min)/g_max;
    RGBInput[2] = (float(rgb[2])-b_min)/b_max;
    
    // Monitoring read value
    // Serial.print("Measured B, R, G: ");
    // Serial.println(String(RGBInput[2])+","+String(RGBInput[0])+","+String(RGBInput[1]));    

    // ニューラルネットで読み込み値を修正
    // predict関数の処理時間を測定（最初の5回のみ出力）
    unsigned long start_time = micros();
    predict(RGBInput, RGBOutput);
    unsigned long end_time = micros();
    unsigned long predict_time = end_time - start_time;
    
    if (predict_time_count < 5) {
        Serial.print("Predict time: ");
        Serial.print(predict_time);
        Serial.println(" microseconds");
        predict_time_count++;
    }

    // predict();が存在しない初期は、値を単純に同値をコピー
    //RGBOutput[0] = RGBInput[0];
    //RGBOutput[1] = RGBInput[1];
    //RGBOutput[2] = RGBInput[2];

    // 0-255でクリッピング
    RGBOutput[0] = min(max(RGBOutput[0]*255, 0), 255);
    RGBOutput[1] = min(max(RGBOutput[1]*255, 0), 255);
    RGBOutput[2] = min(max(RGBOutput[2]*255, 0), 255);

    // Serial.print("Predicted B, R, G: ");
    //Serial.println("Blue:"+String((int)RGBOutput[2]) + ", Red:"+String((int)RGBOutput[0]) + ", Green:"+ String((i3nt)RGBOutput[1]));
    // Serial.println("Red:"+String(RGBInput[0]) + ", Green:"+ String(RGBInput[1]) + ", Blue:"+String(RGBInput[2]));
}

// このリセットコードは旧版Arduino用、Arduino UNO R4 WiFiでは不要
// void softwareReset() {
//     asm volatile ("jmp 0");  // プログラムカウンタをリセット
// }


void setup() {
  // put your setup code here, to run once:
    Serial.begin(9600);
    pinMode(led_r_pin, OUTPUT);
    pinMode(led_g_pin, OUTPUT);
    pinMode(led_b_pin, OUTPUT);
    analogWrite(led_r_pin, 0);
    analogWrite(led_b_pin, 0);
    analogWrite(led_g_pin, 0);

    pinMode(buttonInputPin, INPUT_PULLUP);
    pinMode(buttonMinMaxPin, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(buttonInputPin), buttonInput, FALLING);
    attachInterrupt(digitalPinToInterrupt(buttonMinMaxPin), buttonMinMax, FALLING);

    Serial.println("画像の Height（行数）を入力してください: ");
    while (Serial.available() == 0) {
        // 何もせずに待機
    }
    N = Serial.parseInt();
    M = N*3; // RGBの値を入れるために3倍しておく
    Serial.println("Height: " + String(N));
    Serial.println("Width: " + String((int)(M/3)));
    array = allocateArray(N, M);

    if (array == NULL) {
        Serial.println("配列の確保に失敗");
        return;
    }

    // 配列を初期化
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < M; j++) {
            array[i][j] = 0;
        }
    }
}

void loop() {
  // put your main code here, to run repeatedly:
    readAndProcess();

    if (buttonInputPressed) {
        array[n][m]   = RGBOutput[0];
        array[n][m+1] = RGBOutput[1];
        array[n][m+2] = RGBOutput[2];

        Serial.println(String(n)+" 行目 "+String((int)(m/3))+" 列目 の画素を読み込みました。");
        Serial.println("Red:"+String(RGBInput[0]* 100) + ", Green:"+ String(RGBInput[1] * 100) + ", Blue:"+String(RGBInput[2] * 100));

        if((m+=3)>=M){
            m = 0 ;
            if((n+=1)>=N){
                n = 0;
                Serial.println("画像の読み取りが完了しました。");
                Serial.println("以下のテキストを画像ファイル（.ppm）に貼り付けてください。");
                Serial.println("--------------------------------------------------");
                Serial.println("P3");
                Serial.println(String(N)+" "+String((int)(M/3)));
                Serial.println("255");
                for (int i = 0; i < N; i++) {
                    for (int j = 0; j < M; j++) {
                        Serial.print((int)array[i][j]);
                        Serial.print(" ");
                    }
                    Serial.println();
                }
                Serial.println("--------------------------------------------------");
                Serial.println();
                delay(500); 
                //softwareReset();
                NVIC_SystemReset();
            } 
        }
        buttonInputPressed = false;
    }

    if (buttonMinMaxPressed) {
        if(pushed%2==0){
            r_min=rgb[0], g_min=rgb[1], b_min=rgb[2];
            Serial.println("最小値が更新されました："+String(b_min)+","+String(r_min)+","+String(g_min));
        }
        else{
            r_max=rgb[0]-r_min, g_max=rgb[1]-g_min, b_max=rgb[2]-b_min;
            Serial.println("最大値が更新されました："+String(b_max)+","+String(r_max)+","+String(g_max));
        }
        buttonMinMaxPressed = false;  // フラグをリセット
        pushed++;
        delay(500);
    }

}
