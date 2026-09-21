/**************************************************************************//**
 * @file    cosmoz.h
 * @brief   Cosmo-Z library headder
 * @version V2.4.4
 * @date	17. Jan 2020.
 * @author	Tokushu Denshi Kairo Inc.
 * @note	Copyright &copy; 2015-2020 Tokushu Denshi Kairo Inc. All rights reserved.
 * @details Cosmo-Z Firmware Library
 ******************************************************************************/

#ifndef H_COSMOZ
#define H_COSMOZ

#include <stdio.h>
#include <stdint.h>
#include <time.h>

// 基本的な値の定義

#define FIRMWARE_VERSION 0x02040400
#define FIRMWARE_DATE    0x20200117

#define MAX_REGS 64 //! FPGAが持つ内蔵レジスタの数
#define MAX_EXTCH 4

#ifndef NULL
#define NULL ((void *)0) //!< ぬるぽ
#endif

#ifndef BOOL
#define BOOL int //!< C言語ではBOOLがないのでintでtypedefしておく
#endif

#ifndef TRUE
#define TRUE 1 //!< C言語ではBOOL型がないので1に定義しておく
#endif

#ifndef FALSE
#define FALSE 0 //!< C言語ではBOOL型がないので0に定義しておく
#endif

//! @brief ADCの情報を調べた結果が入る構造体
typedef struct ADC_INFO {
	int    basefreq;           //!< デシメーションされる前のベースとなるクロック周波数
	int    decimation_ratio;   //!< デシメーション比
	double sampling_freq_mhz;  //!< デシメーションされたサンプリング周波数
	int    adc_num;            //!< ADCの数 (Cosmo-Zならばチャネル数の1/4になる)
	BOOL   testmode;           //!< テストモードか否か
	int    device_max_freq;    //!< ADCの最高クロック周波数
	int    device_resolution;  //!< ADCの持つ本来の分解能(bit)
	int    current_resolution; //!< 現在の分解能(bit)
	char   device_type[8][32]; //!< ADCの型番が文字列で入る。最大8個のADCを扱える。
	int    maxfreq[8];         //!< 各ADCの最大周波数。最大8個
	int    clkfreq;            //!< ADCのクロック周波数
	int    div;                //!< 分周比。デシメーションともいう。
	int    cif;                //!< CIFフィルタを使っている場合は、CIFのデシメーション比。使用していなければ1
} ADC_INFO ;

//! @brief アナログチャネルの情報
//! @note (max_voltage - min_voltage) / resolution / gain が1LSBの電圧ステップとなるので意外と重要
//! @warning この情報はまだ使用されていない
typedef struct ANALOG_CH_INFO {
	double input_min_voltage;  //!< 最小入力電圧(V)
	double input_max_voltage;  //!< 最大入力電圧(V)
	double filter_fc;          //!< アナログフロントエンドのLPFのカットオフ周波数(MHz)
	double gain;               //!< アナログフロントエンドのゲイン。真数で示す。
} ANALOG_CH_INFO;

// ユーザ公開 API関数の定義
#if __cplusplus
extern "C" {
#endif

/***********************************************************************************!
//　ドライバ基本関数
************************************************************************************/

/**
 * @brief ドライバをオープンし、その他の雑多な初期化処理を行う。
 * @return オープンに失敗した場合(ドライバがない、ドライバのアクセス権限がない)にはFALSEを返す\n
 * 通常はTRUEを返す。
 * @todo   将来的にはリモートでオープンしたり、USB経由でオープンしたりするための引数が入るだろう。
*/
BOOL csz_open();

/**
 * @brief ファームウェアのバージョンを返す
*/
uint32_t csz_firmware_version();

/**
 * @brief ファームウェアの日付を返す
*/
uint32_t csz_firmware_date();

/**
 * @brief ドライバのバージョンを返す
*/
uint32_t csz_driver_version();

/**
 * @brief FPGAのバージョンを返す
*/
uint32_t csz_fpga_version();

/*!
ドライバをクローズし、その他の雑多なメモリの開放や環境の復元を行います。
@brief CosmoZライブラリをクローズする関数
*/
void csz_close();

/**
 * @brief 48bitタイムスタンプの値を返す
 * @return FPGAが起動してからの時間を10ns単位で計った値
 */
uint64_t csz_time48();

/***********************************************************************************!
//　低レベルなメモリ/レジスタ操作関数
************************************************************************************/

/*!
@brief  FPGA内蔵レジスタの読み出し
@param  regnum レジスタ番号0～(MAX_REGS - 1)。デフォルトのFPGAでは63まで。
@return レジスタの値が帰る。
@note   範囲外のレジスタを読みだすと-1が帰る。この時エラー @ref CSZERR_ILLEGAL_REGNUM がセットされる
*/ 
uint32_t csz_reg_read(int regnum);

/*!
@brief  FPGA内蔵レジスタへ書き込み
@param  regnum レジスタ番号0～(MAX_REGS - 1)。デフォルトのFPGAでは63まで。
@param  val 書き込む値
@return 書き込みに成功するとTRUEを、失敗するとFALSEを返す
@note   範囲外のレジスタへ書き込もうとすると失敗し、 @ref CSZERR_ILLEGAL_REGNUM をセットする
*/ 
// 
BOOL csz_reg_write(int regnum,uint32_t val);

/*!
FPGAが計測したデータが入るメモリ(DMAバッファ)の内容を読み出す。
Cosmo-Zでは物理アドレス0x20000000～0x3fffffffがDMAバッファであるが、
引数で指定する値は0～0x1fffffffとする。
自動的にオフセットとして0x20000000が加わる。
@brief  DMA用メモリのリード
@param  addr 読み込みたいアドレス(Cosmo-Zでは0～0x1fffffff) バイト単位
@param  len 読み込むサイズ。バイト単位
@param  [out] buf 読み込んだ値が格納されるバッファ
@return 読み込んだバイト数。読み込むサイズと一致していなければエラーである。
@warning mallocやnewで確保したバッファは読み込んだデータが格納される十分な大きさがなければならない。
 確保したサイズより大きなデータを読み込むと、システムが不安定になり、読み込んだバイト数が一致しなくなる。また、segmentation faultが発生する。
*/
int csz_mem_read(uint32_t addr, int len, void *buf);

/*!
@brief  DMA用メモリのライト
@param  addr 書き込みたいアドレス(Cosmo-Zでは0～0x1fffffff)
@param  len 書き込むサイズ。バイト単位
@param  [in] buf 書き込むデータが格納されているバッファ
@return 書き込んだバイト数。サイズと一致していなければエラーである。
@note   バッファは書き込むだデータが格納される十分な大きさがなければならない
@warning mallocやnewで確保したバッファサイズより大きなデータを読み込むと、システムが不安定になる
*/
int csz_mem_write(uint32_t addr, int len, void *buf);

/*!
@brief  メモリの情報を調べる
*/
void csz_mem_info(uint32_t *base_addr, ssize_t *size);

/*!
@brief  メモリの内容をダンプする
*/
void csz_mem_dump(int offset, int len);

/*!
@brief  メモリのテストを行う
*/
BOOL csz_mem_test();

/*!
@brief  DMA用メモリの生のアクセスを行うための仮想アドレスを取得する低レベル関数
@param  offset バイト単位のオフセット
@return DMAメモリへのポインタ
@warning 通常は使う必要はない。FPGAが変わればDMAバッファの内容も変わるので注意
*/
unsigned char *csz_mem_vaddr(int offset);

/*!
@brief  最終エラーの内容をテキストで受け取る
*/
char *csz_get_error();


/***********************************************************************************!
//　ADC操作関数
************************************************************************************/

/*!
@brief  現在のADCの値を取得する
@param  ch 調べたいADCのチャネル番号。1～32の範囲で指定すること。
@return 読み出したADCの値が返る。
@details  関数を呼び出したタイミングでサンプリングされる。チャネルの同期などは行われない。<br>
        チャネル番号が範囲外のときには-1が返され、エラーコード  @ref CSZERR_ILLEGAL_CHNUM がセットされる。
@note この関数はFPGA内レジスタREGOFS_RAWADCを書き換える。
*/
uint32_t csz_adc_val(int ch);

/*!
@brief  ADCをソフトリセットし、同期を取り直す
@note   この関数は実行するのに200msかかる。
*/
void csz_adc_reset();

/*!
@brief  ADCの周波数を設定し、同期をリセットする。
@param  freq サンプリング周波数
          - 1 : クロック周波数 80MHz , デシメーション80に設定する
          - 2 : クロック周波数 80MHz , デシメーション40に設定する
          - 4 : クロック周波数 80MHz , デシメーション20に設定する
          - 5 : クロック周波数 80MHz , デシメーション16に設定する
          - 8 : クロック周波数 80MHz , デシメーション10に設定する
          - 10 : クロック周波数 80MHz , デシメーション8に設定する
          - 16 : クロック周波数 80MHz , デシメーション5に設定する
          - 20 : クロック周波数 80MHz , デシメーション4に設定する
          - 25 : クロック周波数 100MHz , デシメーション4に設定する
          - 40 : クロック周波数 80MHz , デシメーション2に設定する
          - 50 : クロック周波数 100MHz , デシメーション2に設定する
          - 80 : クロック周波数 125MHz , デシメーション1に設定する
          - 100 : クロック周波数 125MHz , デシメーション1に設定する
          - 125 : クロック周波数 125MHz , デシメーション1に設定する
          - それ以外  エラーとなる
@return  設定できない値の場合はFALSEを返し、エラーコード  @ref CSZERR_UNSUPPORTED_FREQ を設定する。
@warning  25MHzと50MHzはベース周波数100MHzなので、80MHzのADCでは本来は使用できない。
*/
BOOL csz_adc_freq(int freq);

/*!
@brief  現在のADCのサンプリング周波数を取得する
@return  サンプリング周波数 (1～125)。なお、
@details ADCに与えているクロックの周波数ではなく、デシメーション後の周波数
*/
int csz_adc_getfreq();

/*!
@brief  ADCに関する情報を取得する
@return  検出されたADCの情報をADC_INFO構造体に入れて返す
*/
void csz_adc_getinfo(ADC_INFO *info);

/*!
実際にADCをテストモードにするのではなく、FPGA内でテストデータを発生させる。\n
Cosmo-Zでは、FPGA内の演算によって、指数関数的に減衰する波形「exp(-t/τ)」をLPFでなまらせたような波形を発生させる。
@brief  ADをがテストモードにする
@param  on TRUEならばテストモードにする。\n
           FALSEならば通常動作モードにする
@param  option テストモードのオプション設定(通常は0にする)
*/
void csz_adc_testmode(BOOL on,int option);

/*!
@brief  ADCがテストモードかどうかを調べる
@return テストモードならばTRUEを、通常動作ならばFALSEを返す
*/
BOOL csz_adc_is_testmode();

/**
 * @brief ADCのチャンネル数を取得する
 * @return 現在のFPGAの構成で使用可能なADCのチャンネル数を返す。Cosmo-Zならば8,16,24,32のいずれかである。
*/
int csz_adc_maxch();

/**
 * @brief キャリブレーションの結果を表示(テスト用)
 */
void csz_adc_calstat();

/**
 * @brief どのようなADCが接続されているか調べ上げる
 */
void csz_adc_enumerate(int *adcnum,char partnum[8][32]);

/**
 * @brief ADCのレジスタを表示
 */
void csz_adc_regdump();

/**
 * @brief ADCの内部レジスタに値を書き込む
 */
void csz_adc_regwr(uint16_t addr,uint8_t data);

/**
 * @brief ADCの内部レジスタの値を読み出す
 */
uint8_t csz_adc_regrd(uint16_t addr);

/***********************************************************************************!
// 波形キャプチャ関数
************************************************************************************/

//! @brief 波形キャプチャの現在の状態
typedef enum {
	CAPSTAT_IDLE,            //!< 待機状態
	CAPSTAT_TRIGWAIT,        //!< トリガ待ち状態
	CAPSTAT_RUN,             //!< 実行中
} CAPTURE_STATUS;

//! @brief 波形キャプチャの実行結果
typedef enum {
	CAPRESULT_SUCCESS,         //!< キャプチャは成功した
	CAPRESULT_ABORTED,         //!< キャプチャはユーザからの指示で中断された
	CAPRESULT_ERROR,           //!< データの転送が間に合わなかったなどの理由で失敗した
	CAPRESULT_ANOTHERRUN,      //!< 他のキャプチャが動作しているため、波形キャプチャを開始できない
	CAPRESULT_TRIGGER_TIMEOUT, //!< トリガ待ち状態のままで、タイムアウトした
	CAPRESULT_CORRUPTED,       //!< 取得したデータが壊れている
} CAPTURE_RESULT;

//! @brief 波形キャプチャの方法を示す
typedef enum  {
	CAPTRIG_AUTO,           //!< オシロのAUTOモードのように、少しだけトリガを待ってからキャプチャ
	CAPTRIG_NORMAL,         //!< トリガが来るまで待ってからキャプチャする。オシロのNORMALモードに相当
	CAPTRIG_IMMEDIATE,      //!< 即座にキャプチャを開始する
	CAPTRIG_LONG,           //!< 長時間キャプチャ
	CAPTRIG_MESUNIT,        //!< イベントモードでのキャプチャ(秒数で指定)
	CAPTRIG_EVENT,          //!< イベントモードでのキャプチャ(イベント数で指定)
	CAPTRIG_LAST            //!< 最後に表示した波形を再取得
} CAPTURE_TRIGGER;

//! @brief 波形キャプチャの実行結果
typedef struct CAPTURE_INFO {
	CAPTURE_RESULT result; //!< キャプチャ実行結果
	uint32_t chmask;       //!< キャプチャしたチャネル
	int chcount;           //!< キャプチャしたチャネル数
	int length;            //!< キャプチャ長
	int iteration;         //!< 繰り返し回数
	int maxseq;            //!< 最大シーケンス長
	CAPTURE_TRIGGER trig;  //!< キャプチャトリガの種類
	time_t start_time;     //!< 開始時刻
	time_t end_time;       //!< 終了時刻
	double sampling_rate;  //!< サンプリングレート
	int adc_freq;          //!< 実行開始時のADC周波数
	int adc_div;           //!< 実行開始時のデシメーション比
	int resolution;        //!< 分解能(bit単位)
	uint32_t gps_time;     //!< 実行開始時のGPS時刻
	uint32_t gps_10ns;     //!< 実行開始時のGPS時刻 10ns単位
	uint32_t gps_maxcount; //!< 実行開始時のGPS最大カウント
	uint64_t start_time48; //!< 実行開始時の48bitタイムスタンプ
	uint32_t top_addr;     //!< 格納される物理アドレスの先頭
	uint32_t bot_addr;     //!< 格納される物理アドレスの最後尾
	uint32_t last_addr;    //!< 格納された物理アドレスのlast
	// 以下はイベントキャプチャで使う
	int mesp;              //!< 現在読み出し中のデータのポインタ
	int evcount;           //!< 現在読み出し中のデータの数
	uint32_t prev_capaddr; //!< イベントキャプチャのデータをここまで読んだポインタ
	uint32_t start_addr;   //!< イベントキャプチャのデータが入っている先頭アドレス
} CAPTURE_INFO;

/*!
@brief  波形キャプチャを実行する
@param  chmask キャプチャしたいチャネルを2進数のビットマスクで指定する。<br>
               例えば、CH1とCH3とCH7,8をキャプチャしたい場合には0xc5とする。
@param  length キャプチャしたい長さを、データ点数で指定する。
@param  type キャプチャの方法を指定する
@param  maxseq 最大シーケンス長。ここで指定された回数、キャプチャ動作を繰り返す
@return キャプチャの実行結果構造体
@note   キャプチャした結果のデータは、csz_capture_datacopy関数を使って得る
*/
CAPTURE_INFO csz_capture_execute(uint32_t chmask,int length,CAPTURE_TRIGGER type,int maxseq);

/*!
@brief  キャプチャされた波形データをユーザバッファにコピーする
@param  csz_capture_execute()で得られた実行結果構造体 <br>
@param  ch 取得したいチャネルの番号 (1-32)
@param  [out] result データがコピーされるユーザバッファ。uint16_tである。
*/
void csz_capture_datacopy(CAPTURE_INFO info, int ch, uint16_t *result);

/*!
@brief  キャプチャされた波形データを32bitとしてユーザバッファにコピーする
@param  csz_capture_execute()で得られた実行結果構造体 <br>
@param  ch 取得したいチャネルの番号 (1-32)
@param  [out] result データがコピーされるユーザバッファ。uint32_tである。
*/
void csz_capture_datacopy32(CAPTURE_INFO info, int ch, uint32_t *result);

/**
 * キャプチャを開始してもトリガが入らない場合にタイムアウトする時間を設定する。\n
 * タイムアウトすると csz_capture_execute() は CAPRESULT_TRIGGER_TIMEOUT を返す
 * @brief  タイムアウトを指定する
 * @param ms タイムアウト時間。ms単位。
*/
void csz_capture_timeout(int ms);

/**
 * @brief キャプチャのタイムアウト時間を取得する
*/
int csz_capture_get_timeout();

/*!
@brief  現在の波形キャプチャを中断する
*/
void csz_capture_abort();

/*!
@brief  現在のキャプチャの状態(RUNか、IDLEか、TRIGWAITか)を返す
*/
CAPTURE_STATUS csz_capture_status();

/***********************************************************************************!
* イベントキャプチャ
************************************************************************************/

/*!
イベント波形のヘッダ。波高値や発生時刻などが得られる。
*/
typedef struct EVHEADER_STR {
	int      evcount; //!< このイベントが何番目のイベントか
	uint16_t hdr;     //!< ヘッダ情報
	uint8_t  ch;      //!< 発生したイベントの個数
	uint16_t length;  //!< 波形の長さ
	uint64_t time48;  //!< 発生した時刻。FPGAの起動時から10ns単位で
	uint16_t plsh;    //!< パルスハイト
	uint16_t sum;     //!< パルスの面積
	uint16_t ped;     //!< ペデスタル(ベースライン)レベル
	uint16_t meslen;  //!< トリガがかかっている長さ
	int      mesp;    //!< 不明
} EVHEADER_STR;

/*!
@brief  イベントが発生するたびに呼び出されるコールバック関数。ユーザが用意する。
@note   この関数は用意しなくてもよい。用意した場合は、例えば、波形データをネットワークで送信するなどの処理を行う。用意しない場合はシステムがデフォルトの処理を行う。
@param  data 波形データが入っているバッファへのポインタ
@param  bytelen 波形データの長さ(バイト単位)
*/
typedef BOOL (*CALLBACK_EVENTCAP)(uint16_t *data, int bytelen, void *param);

/*!
@brief  イベントキャプチャを実行する
@param  chmask チャネルマスク
@param  count 最大で何個のイベントをキャプチャするか
@param  type CAPTRIG_EVENT または CAPTRIG_MESUNIT
@param  func イベント発生時に呼び出されるユーザ定義のコールバック関数。詳細はCALLBACK_EVENTCAP() を参照。
@param  param コールバック関数に渡される引数。
@return キャプチャ情報構造体を返す。この構造体をcsz_evcap_get_nextに渡す。
*/
CAPTURE_INFO csz_evcap_execute(uint32_t chmask,int count,CAPTURE_TRIGGER type, CALLBACK_EVENTCAP func, void *param);

/*!
@brief  イベントキャプチャの結果を得るための関数
@param  info キャプチャ情報構造体。csz_evcap_execute() の結果を与える。
@param  [out] hdr イベント波形のヘッダがセットされて戻る
@param  [out] data イベント波形のデータがセットされて戻る
*/
BOOL csz_evcap_get_next(CAPTURE_INFO *info,EVHEADER_STR *hdr,uint16_t *data);

/***********************************************************************************!
* トリガの設定
************************************************************************************/

//! @brief トリガの方法を示す
typedef enum  {
	TRIGTYPE_OFF,           //!< トリガは発生しない
	TRIGTYPE_UPPER,         //!< 指定値以上ならトリガ発生
	TRIGTYPE_LOWER,         //!< 指定値以下ならトリガ発生
	TRIGTYPE_CROSS,         //!< 指定値を横切ったらトリガ発生
	TRIGTYPE_DISCRI,        //!< 通常の値より指定値分下回ったらトリガ発生
	TRIGTYPE_RISE,          //!< 指定値を横切って立ち上がったらトリガ発生
	TRIGTYPE_FALL,          //!< 指定値を横切って立ち下がったらトリガ発生
	TRIGTYPE_LINKAGE,       //!< 他のチャネルがヒットしたらトリガ発生
	TRIGTYPE_USER,          //!< ユーザ定義による
	TRIGTYPE_ERROR          //!< エラー
} TRIGTYPE;

//! @brief ディジタル入力トリガの方法を示す
typedef enum  {
	EXTTRIGTYPE_OFF,        //!< トリガは発生しない
	EXTTRIGTYPE_RISE,       //!< 立ち上がりでトリガ発生
	EXTTRIGTYPE_FALL,       //!< 立ち下がりでトリガ発生
	EXTTRIGTYPE_HIGH,       //!< Hレベルでトリガ発生
} EXTTRIGTYPE;

/**
 * @brief トリガをORトリガ(全チャネルのうちの1つでもヒットしたら、全体のトリガをかける)に設定する
 */
void csz_trig_ortrig(BOOL ortrig);

/**
 * @brief 現在ORトリガが適用されているかを調べる
 */
BOOL csz_trig_is_ortrig();

/**
 * @brief トリガをセットする
 * @param ch チャネル番号 (1-32)
 * @param type トリガのかけ方
 * @param val  トリガをかける値。ADC値で設定する。LinkageとOffの場合は、値は設定されるが動作には関係ない。
 * @return 成功したらTRUEを返す。CH番号が逸脱しているか、値が逸脱していたらFALSEを返し、err番号を設定する。
 */
BOOL csz_trig_set(int ch,TRIGTYPE type, int val);

/**
 * @brief 電圧でトリガをセットする
 * @param ch チャネル番号 (1-32)
 * @param type トリガのかけ方
 * @param voltage  トリガをかける値。電圧で設定する。LinkageとOffの場合は、値は設定されるが動作には関係ない。
 * @return 成功したらTRUEを返す。CH番号が逸脱しているか、値が逸脱していたらFALSEを返し、err番号を設定する。
 */
BOOL csz_trig_set_voltage(int ch, TRIGTYPE type, double voltage);

/**
 * @brief トリガのタイプを取得する
 * @param ch チャネル番号 (1-32)
 * @param [out] type  トリガの種類
 * @param [out] voltage  トリガをかかる値。ADC値で示される。
 * @return 成功したらTRUEを返す。CH番号が逸脱していればFALSEを返し、err番号を設定する。
 */
TRIGTYPE csz_trig_gettype(int ch);

/**
 * @brief トリガのADC値を取得する
 * @param ch チャネル番号 (1-32)
 * @return トリガがかかるADC値
 */
int csz_trig_getadcval(int ch);

/**
 * @brief トリガの値を電圧として取得する
 * @param ch チャネル番号 (1-32)
 * @return トリガがかかる電圧
 */
double csz_trig_getvoltage(int ch);

/**
 * @brief トリガのタイプ \ref TRIGTYPE を文字列にして返す
 * @return トリガタイプの意味を表す文字列
 */
const char *csz_trig_tostr(TRIGTYPE type);

/**
 * @brief トリガのカウントレートを調べる
 */
int csz_trig_rate(int ch);

/**
 * @brief トリガがかかっているパルスハイトを調べる
 */
int csz_trig_pulseheight(int ch);

/**
* @brief トリガがかかっていない状態のADC値を調べる
*/
int csz_trig_pedestal(int ch);

/**
 * @brief チャネルトリガはなく全体トリガでキャプチャするかどうかの設定
 * @param ch チャネル番号 (1-32)
 * @param enable 全体トリガでキャプチャする
 */
void csz_trig_set_gcap(int ch, BOOL enable);

/**
 * @brief チャネルトリガはなく全体トリガでキャプチャするかどうかを調べる
 * @param ch チャネル番号 (1-32)
 * @return 全体トリガでキャプチャするならtrueを返す
 */
BOOL csz_trig_get_gcap(int ch);

/**
 * @brief プレトリガ(トリガ点より前に何ポイント取るか)を設定する
 */
BOOL csz_pretrig_set(int pre);

/**
 * @brief プレトリガのタイプを指定
 * @param extend FALSEならばトリガがかかった時点から
 * @note この関数はまだ実装されていない
 */
void csz_pretrig_type(BOOL extend);

/**
 * @brief 現在のプレトリガを取得する
 */
int csz_pretrig_get();

/**
 * @brief FPGAに設定可能なプレトリガの最大値を調べる
 * @note この関数はまだ実装されていない
 */
int csz_pretrig_max();

/**
 * @brief ポストトリガの長さを設定する
 */
BOOL csz_posttrig_set(int pre);

/**
 * @brief ポストトリガの長さを取得する
 */
int csz_posttrig_get();

/**
 * @brief ポストトリガのタイプを固定長にする
 */
void csz_posttrig_fixed(BOOL fixed);

/**
 * @brief ポストトリガのタイプが固定長であるか調べる
 */
BOOL csz_posttrig_is_fixed();

/**
 * @brief 外部トリガを設定
 * @param extch 0-3 チャネル番号
 * @param type 外部トリガタイプ
*/
BOOL csz_exttrig_set(int extch, EXTTRIGTYPE type);

/**
 * @brief 外部トリガのタイプを文字列にする
*/
const char *csz_exttrig_tostr(EXTTRIGTYPE type);

/**
 * @brief 外部トリガの状態を取得
 * @param extch 0-3 チャネル番号
*/
EXTTRIGTYPE csz_exttrig_get(int extch);

/***********************************************************************************!
//　GPS関係
************************************************************************************/

/**
 * @brief GPSに同期した時刻を得る
 * @param [out] gps_time GPSの1pps信号で数えた時刻
 * @param [out] gps_10ns 1pps間の10nsクロックの現在のカウント数
 * @param [out] gps_maxcount 10nsクロックが何個でGPSの1ppsとなるか
 * @note FPGAはGPSからの1PPS信号の周期を内部のクロックカウンタで測っている。\n
 * maxcountを使ってgps_10nsを校正しなければならないが、わかりにくい。
 * @result GPSが使える場合はTRUEを返す。使えない場合はFALSEを返す。
 * @note この関数を使用する前に、csz_gps_settime()を呼び出すこと
 */
BOOL csz_gps_time(uint32_t *gps_time, uint32_t *gps_10ns, uint32_t *gps_maxcount);

/**
* @brief GPS時刻を設定する
* @param 現在のGPS時刻。Unix時刻形式で与える
*/
void csz_gps_settime(time_t time);

/***********************************************************************************!
* XADC処理
************************************************************************************/

/**
 * @brief FPGAの温度を返す
*/
double csz_xadc_tempe();

/**
 * @brief FPGAのコア電圧を返す
*/
double csz_xadc_vccint();

/**
 * @brief FPGAのAUX電圧を返す
*/
double csz_xadc_vccaux();

/**
 * @brief FPGAのDDR電圧を返す
*/
double csz_xadc_vccddro();

/***********************************************************************************!
* エラー処理
************************************************************************************/

/*!
 @brief エラーの定義
 @todo 実際の数値をあてはめよう
 */
typedef enum {
	CSZERR_ILLEGAL_REGNUM,    //!< 範囲外のレジスタ番号
	CSZERR_MEMRANGE_OVERFLOW, //!< メモリアクセスの範囲がオーバーフローした
	CSZERR_MEMADDR_ALIGNMENT, //!< メモリアクセスが4バイトの単位になっていない(エラーにはならない)
	CSZERR_UNSUPPORTED_FREQ,  //!< 設定できない周波数に設定しようとした
	CSZERR_ILLEGAL_CHNUM,     //!< 設定できないチャネル番号が指定された
	CSZERR_TRIGRANGE_OVER,    //!< トリガの値が逸脱した
} COSMOZ_ERROR;

/**
 * @brief  エラーのセット
*/
void csz_set_error(COSMOZ_ERROR err);

/***********************************************************************************!
* その他
************************************************************************************/

/**
 * @brief 現在のADCのサンプリング速度の設定で、データ点数を時間に変換する
 * @param point データ点数
 * @return 時間[us]単位
 */
double csz_point2us(int point);

/**
 * @brief 現在のADCのサンプリング速度の設定で、時間をデータ点数に変換する
 * @param us 時間[us]単位
 * @return データ点数
 */
int csz_us2point(double us);

/*!
@brief  CTRL+Cを無効にする
*/
void disable_ctrlc();

/*!
@brief  ctrlcが押されたことを知る
*/
BOOL is_crtlc_pushed();

#if __cplusplus
}
#endif

#endif /* H_COSMOZ */
