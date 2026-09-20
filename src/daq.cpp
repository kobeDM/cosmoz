#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <math.h> // for fabs

#include <thread>
#include <atomic>
#include <chrono>

#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>

#include "cosmoz.h"
#include "picojson.hpp"
#include "tools_FPGA.h"

const int nChMax = 8;

using namespace std;

bool readJson(const string &file, picojson::array &aAry)
{
   stringstream ss;
   ifstream ifs(file.c_str());
   ss << ifs.rdbuf();
   ifs.close();

   picojson::value v;
   ss >> v;
   string err = picojson::get_last_error();
   if (!err.empty()) {
      cerr << err << endl << "Json miss" << endl;
      return false;
   }

   aAry = v.get<picojson::array>();
   return true;
}

// trigwait 時間の監視
std::atomic<bool> monitorCapture{false};

struct LiveTimeInfo {
    double idle = 0.0;
    double trigwait = 0.0;
    double run = 0.0;
};

void monitorCaptureStatus(LiveTimeInfo* info)
{
    using clock = std::chrono::steady_clock;

    CAPTURE_STATUS prevState = csz_capture_status();
    auto prevTime = clock::now();

    while (monitorCapture.load()) {

        // 10 usごと
        std::this_thread::sleep_for(std::chrono::microseconds(10));

        auto now = clock::now();
        CAPTURE_STATUS state = csz_capture_status();

        double dt = std::chrono::duration<double>(now - prevTime).count();

        switch (prevState) {
        case CAPSTAT_IDLE:
            info->idle += dt;
            break;

        case CAPSTAT_TRIGWAIT:
            info->trigwait += dt;
            break;

        case CAPSTAT_RUN:
            info->run += dt;
            break;
        }

        prevTime = now;
        prevState = state;
    }

    // 最後の区間
    auto now = clock::now();
    double dt = std::chrono::duration<double>(now - prevTime).count();

    switch (prevState) {
    case CAPSTAT_IDLE:
        info->idle += dt;
        break;
    case CAPSTAT_TRIGWAIT:
        info->trigwait += dt;
        break;
    case CAPSTAT_RUN:
        info->run += dt;
        break;
    }
}


// trigger 数の監視
std::atomic<bool> monitorTrigger{false};

struct TriggerMonitor {
    double   triggerRate[nChMax] = {}; // ch毎のトリガー発生数
    uint64_t nRate[nChMax] = {}; // ch毎のトリガー発生数
};

void monitorTriggerRate(TriggerMonitor* mon, const std::vector<uint16_t>& chs)
{
    using namespace std::chrono;

    // csz_trig_rate() を約0.1秒ごとに読む
    while (monitorTrigger.load()) {

        if (!monitorTrigger.load()) break;

        for (auto ch : chs) {
            int rate = csz_trig_rate(ch);
            if (rate >= 0) {
                mon->triggerRate[ch - 1] += static_cast<double>(rate);
                mon->nRate[ch - 1]++;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}


#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>

static int g_status = 0;

// 割り込み
void handler(int signum)
{
   if (signum == SIGINT) { // CTRL+Cが押された場合
      printf("\nCTRL+C pushed.\n");
   }
   g_status = 1;
}

class DataAnalysis
{
public:
   uchar ch; // チャンネル
   uint32_t timeStamp; // 計測開始からキャプチャまでのタイムスタンプ (ms 単位)

   uchar nMore; // 全区間で何回 pedestal + analyThr を上回ったか
   uchar nLess; // 全区間で何回 pedestal - analyThr を下回ったか

   // [0] : preTrigまで
   // [1] : preTrig から DataSize
   int16_t max[2], min[2]; // ペデスタルを差し引くので負の値もとる

   float pedestal; // 0 ~ preTrig - 5 の平均値

   // [0] : データ部分前半（preTrig から 0.5*capLength）
   // [1] : データ部分後半（0.5*capLength から DataSize）
   uint16_t integral[2]; // ペデスタルは差し引くが、絶対値をとる（最大値：65535）

   DataAnalysis()
   : ch(0), timeStamp(0), nMore(0), nLess(0)
   {
      max[0] = 0;
      min[0] = 4095;
      max[1] = 0;
      min[1] = 4095;
      pedestal = 0.;
      integral[0] = 0.;
      integral[1] = 0.;
   }

   int32_t GetIntegral() const
   { return integral[0] + integral[1]; }

   void Write(FILE *ofp)
   {
      fwrite(&ch, sizeof(ch), 1, ofp);
      fwrite(&timeStamp, sizeof(timeStamp), 1, ofp);
      fwrite(&nMore, sizeof(nMore), 1, ofp);
      fwrite(&nLess, sizeof(nLess), 1, ofp);
      fwrite(max, sizeof(max), 1, ofp);
      fwrite(min, sizeof(min), 1, ofp);
      fwrite(&pedestal, sizeof(pedestal), 1, ofp);
      fwrite(integral, sizeof(integral), 1, ofp);
   }
};

bool analysis = false;
int anlyThr = 100;
int anlyNMore =  10000000;
int anlyNLess =  10000000;
double anlyMax0 =  10000; // トリガー前
double anlyMin0 = -10000; // トリガー前
double anlyMax1 =  10000; // トリガー後
double anlyMin1 = -10000; // トリガー後
double anlyIntegral = 0;  // トリガー後
vector<DataAnalysis> vData;

static uint64_t capTime64;
static uint32_t elapsedTime;
static double   liveRate[nChMax] = {}; // ch毎の livetime ratio
static uint32_t nEvent;
static uint64_t nEventCh[nChMax] = {}; // ch毎のイベント記録数
static uint64_t startTimeFPGA;
static uint64_t elapsedTimeFPGA;
static fpos_t pos;
static int preTrig = 10;
static uint32_t capLength = 128;

// イベントが入ったときにコールバックされる関数
static BOOL evcap(uint16_t *data, int bytelen, void *param)
{
   /*
   elapsedTimeFPGA = csz_time48() - startTimeFPGA;
   if (elapsedTimeFPGA > capTime64)
      throw tools::exception("evcap", "Capture End.");
   */

   FILE *ofp = (FILE*)param;
   if (ofp) fwrite(data, bytelen, 1, ofp);  // イベントヘッダ＋波形データ
   nEvent++;

   // 初めの 24 byteはヘッダ
   if ((data[0] & 0xff0f) != 0x0001) { // 00000000????0001 というデータでなければ壊れている
      printf("corrupted! %x\n", data[0]);
      return FALSE;
   }
   int ch = ((data[0] & 0x1f0) >> 4) + 1; // チャンネル
   nEventCh[ch - 1]++;

   if (analysis) {
      DataAnalysis da;
      da.ch = ch;
      uint16_t length = data[1]; // 実際の（意味のある）波形データ長
      uint64_t trigTime = ((uint64_t)data[4] << 32) + ((uint32_t)data[3] << 16) + (uint32_t)data[2]; // トリガタイム (10ns単位)
      da.timeStamp = (uint32_t)((trigTime - startTimeFPGA) * 0.00001); // 10ns単位 -> ms に変換
      uint16_t pedestal = data[8];

      // 波形解析
      length += 12; // ヘッダが uint16_t で 12個分あるので
      uint16_t thrH = pedestal + anlyThr;
      uint16_t thrL = pedestal - anlyThr;
      uint16_t t1 = preTrig + 12; // トリガーまでのデータ点
      uint16_t t2 = t1 + (uint16_t)(0.5*capLength); // トリガーから 0.5*capLength までのデータ点
      float integral0 = 0.;
      float integral1 = 0.;
      for (int i = 12; i < length; i++) {
         if (i >= 13) {
            if (data[i-1] < thrH && data[i] > thrH) da.nMore++; // thrH を下回り --> 上回り
            if (data[i-1] > thrL && data[i] < thrL) da.nLess++; // thrL を上回り --> 下回り
         }
         if (i < t1) { // トリガー前
            if (da.max[0] < data[i]) da.max[0] = data[i];
            if (da.min[0] > data[i]) da.min[0] = data[i];
            if (i < t1 - 5) da.pedestal += data[i];
         }
         else { // トリガー後
            if (da.max[1] < data[i]) da.max[1] = data[i];
            if (da.min[1] > data[i]) da.min[1] = data[i];
            if (i < t2) integral0 += data[i]; // データ前半
            else        integral1 += data[i]; // データ後半
         }
      }
      if (preTrig < 10) da.pedestal = pedestal;
      else              da.pedestal /= 1. * (preTrig - 5);

      da.max[0] -= da.pedestal;
      da.min[0] -= da.pedestal;
      da.max[1] -= da.pedestal;
      da.min[1] -= da.pedestal;

      integral0 = fabs(integral0 - da.pedestal * (t2 - t1));
      integral1 = fabs(integral1 - da.pedestal * (length - t2));
      da.integral[0] = integral0 > 65535 ? 65535 : (uint16_t)integral0;
      da.integral[1] = integral1 > 65535 ? 65535 : (uint16_t)integral1;

      bool isOk = true;
      if (da.nMore > anlyNMore) isOk = false;
      if (da.nLess > anlyNLess) isOk = false;
      if (da.max[0] > anlyMax0) isOk = false;
      if (da.min[0] < anlyMin0) isOk = false;
      if (da.max[1] > anlyMax1) isOk = false;
      if (da.min[1] < anlyMin1) isOk = false;
      if (fabs(da.integral[0] + da.integral[1]) < anlyIntegral) isOk = false;

      if (isOk) vData.push_back(da);
   }

   if (g_status == 1)
      throw tools::exception("evcap", "Capture(Event Mode) Aborted.");

   return TRUE;
}


void usage(const char *prg)
{
   fprintf(stderr,"Version : 20260811\n");
   fprintf(stderr,"Usage : %s  [ params.json ]  ( output_name )\n\n", prg);
}

int main(int argc, char *argv[])
{
   signal(SIGINT, handler); // CTRL+C が押された場合の処理

   FILE *ofp = 0;
   try {
      string parFile = "params.json"; // parameter file
      string header = "out";          // file header
      uint16_t wavelength = 0;        // wavelength
      double timeStampExt = 0.;       // external time stamp

      if (argc < 2) {
         usage(argv[0]);
         throw tools::exception("main", "No parameter file");
      }
      parFile = argv[1];
      if (argc >= 3) header = argv[2];
      if (argc >= 4) wavelength = atoi(argv[3]);
      if (argc >= 5) timeStampExt = atof(argv[4]);

      string waveFile = header +  tools::itos(wavelength) + ".wave";

      // Initialize
      if (csz_open() == FALSE) throw tools::exception("main", "Failed to open cosmoz driver.");

      int maxCH = csz_adc_maxch();
      for (int ic=0; ic<maxCH; ic++) {
         csz_trig_set(ic+1, TRIGTYPE_OFF, 0);
      }

      // Option
      vector<uint16_t> chs;
      int nCh = 0;
      uint32_t chMask = 0x0000;
      uint16_t capMode = 0; // 0 -> オシロ、1 -> イベント
      double capTime = 1.;
      uint32_t capLength = 128;
      int nStore = 1;
      picojson::array aAry;
      if (readJson(parFile, aAry)) {
         auto obj = aAry[0].get<picojson::object>();

         if (obj["Reset"].is<bool>()) {
            bool doReset = obj["Reset"].get<bool>();
            if (doReset) {
               // ADCをソフトリセットし、同期を取り直す。300ms程度要する。
               csz_adc_reset();
            }
         }

         if (obj["Frequency"].is<int>()) {
            int freq = (int)obj["Frequency"].get<double>(); // MHz
            if (!csz_adc_freq(freq)) throw tools::exception("json","Irregal frequency %d",freq);
         }

         for (int ic = 1; ic <= nChMax; ic++) {
            string chName = "Ch" + tools::itos(ic);
            if (obj[chName.c_str()].is<picojson::object>()) {
               auto obj2 = obj[chName.c_str()].get<picojson::object>();
               string chType = "off";
               int adc = 0;
               if (obj2["Type"].is<string>()) chType = obj2["Type"].get<string>();
               if (obj2["ADC"].is<int>()) adc = (int)obj2["ADC"].get<double>();
               TRIGTYPE type = TRIGTYPE_OFF;
               if      (chType == "upper"  ) type = TRIGTYPE_UPPER;
               else if (chType == "lower"  ) type = TRIGTYPE_LOWER;
               else if (chType == "cross"  ) type = TRIGTYPE_CROSS;
               else if (chType == "discri" ) type = TRIGTYPE_DISCRI;
               else if (chType == "rise"   ) type = TRIGTYPE_RISE;
               else if (chType == "fall"   ) type = TRIGTYPE_FALL;
               else if (chType == "linkage") type = TRIGTYPE_LINKAGE;
               if (type == TRIGTYPE_OFF) continue;
               csz_trig_set(ic, type, adc);
               chs.push_back(ic);
               chMask += 1 << (ic-1);
            }
         }
         nCh = chs.size();

         if (obj["OrTrig"].is<bool>()) {
            BOOL isOrTrig = obj["OrTrig"].get<bool>();
            csz_trig_ortrig(isOrTrig);
         }

         if (obj["PreTrig"].is<int>()) {
            preTrig = (int)obj["PreTrig"].get<double>();
            csz_pretrig_set(preTrig);
         }

         if (obj["PostTrigFixed"].is<bool>()) {
            BOOL isPostTrigFixed = obj["PostTrigFixed"].get<bool>();
            csz_posttrig_fixed(isPostTrigFixed);
         }

         if (obj["TimeOut"].is<double>()) {
            double timeout = obj["TimeOut"].get<double>(); // s
            csz_capture_timeout(1000*timeout); // ms
         }

         if (obj["Capture"].is<picojson::object>()) {
            auto obj2 = obj["Capture"].get<picojson::object>();
            if (obj2["Mode"].is<int>()) {
               capMode = (uint16_t)obj2["Mode"].get<double>(); // s
            }
            if (obj2["Time"].is<double>()) {
               capTime = obj2["Time"].get<double>(); // s
            }
            if (obj2["Length"].is<int>()) {
               capLength = (int)obj2["Length"].get<double>();
            }
         }

         if (obj["NStore"].is<int>()) {
            nStore = (int)obj["NStore"].get<double>();
            if (nStore < 1) nStore = 1;
         }

         if (obj["SaveWaveform"].is<bool>()) {
            bool saveWaveform = obj["SaveWaveform"].get<bool>();
            if (saveWaveform) {
               if (!(ofp = fopen(waveFile.c_str(), "wb+")))
                  throw tools::exception("main","Failed to open wave file %s", waveFile.c_str());
            }
         }

         if (obj["Analysis"].is<picojson::object>()) {
            auto obj2 = obj["Analysis"].get<picojson::object>();
            analysis = true;
            if (obj2["Threshold"].is<int>()) anlyThr = (int)obj2["Threshold"].get<double>();
            anlyThr = abs(anlyThr);
            if (obj2["NMore"].is<double>()) anlyNMore = obj2["NMore"].get<double>();
            if (obj2["NLess"].is<double>()) anlyNLess = obj2["NLess"].get<double>();
            if (obj2["Max0"].is<double>()) anlyMax0 = obj2["Max0"].get<double>();
            if (obj2["Min0"].is<double>()) anlyMin0 = obj2["Min0"].get<double>();
            if (obj2["Max1"].is<double>()) anlyMax1 = obj2["Max1"].get<double>();
            if (obj2["Min1"].is<double>()) anlyMin1 = obj2["Min1"].get<double>();
            if (obj2["Integral"].is<double>()) anlyIntegral = obj2["Integral"].get<double>();
         }
      }
      else {
         throw tools::exception("json","Failed to read parameter file %s",parFile.c_str());
      }

      if (capMode == 1) csz_posttrig_set(capLength);

      // Json で指定しなかった場合は現状の設定に更新する
      preTrig  = csz_pretrig_get();

      cout << "------- cosmo-z information -------" << endl;
      cout << "FPGA     : version = " << csz_fpga_version() << endl;
      cout << "Firmware : date = " << csz_firmware_date() << ",  version = " << csz_firmware_version() << endl;
      cout << "Driver   : version = " << csz_driver_version() << endl;
      cout << "-----------------------------------\n" << endl;

      cout << "Sampling Frequency : " << csz_adc_getfreq() << " MSPS\n";
      cout << "Trigger Channels : \n";
      for (uint16_t ic=0; ic<maxCH; ic++) {
         cout << "\tCh" << ic+1 << " : " << csz_trig_tostr(csz_trig_gettype(ic+1)) << " " << csz_trig_getadcval(ic+1) << endl;
      }
      cout << "Or Trigger : " << csz_trig_is_ortrig() << endl;
      cout << "Pre-Trigger : " << preTrig << endl;
      cout << "Post-Trigger Fixed : " << csz_posttrig_is_fixed() << endl;
      cout << "Timeout : " << csz_capture_get_timeout() << " ms\n";
      cout << "Capture time : " << capTime << " s\n";
      cout << "Capture length : " << capLength << "\n";

      CAPTURE_INFO    capInfo;
      CAPTURE_STATUS  capState;
      //EVHEADER_STR   *evStr;

      if (nCh == 0) throw tools::exception("main", "nCh must not be 0");

      time_t startTime = time(NULL);
      startTimeFPGA = csz_time48(); // FPGAを起動してからの時間を10ns単位で計った値
      //elapsedTime = capTime * 1000; // s -> ms に変換
      elapsedTime = 0; // ms
      nEvent = 0;
      if (ofp) {
         fwrite(&timeStampExt,  sizeof(double),   1, ofp);   // 外部からのタイムスタンプ (Unix time)
         fwrite(&startTime,     4,                1, ofp);   // キャプチャ開始時間 (Unix time)
         fwrite(&startTimeFPGA, sizeof(uint64_t), 1, ofp);   // キャプチャ開始時間 (FPGA時間、10ns単位)
         fwrite(&capMode,       sizeof(uint16_t), 1, ofp);   // キャプチャモード (0 -> オシロ、1 -> イベント)
         fwrite(&chMask,        sizeof(uint32_t), 1, ofp);   // キャプチャチャンネル
         fwrite(&capLength,     sizeof(uint32_t), 1, ofp);   // キャプチャ長
         fgetpos(ofp, &pos);  // 後から測定時間、イベント数を記録するためにファイルの位置を取得
         fwrite(&elapsedTime,   sizeof(uint32_t), 1, ofp);   // キャプチャ時間 (ms)（最後に実際の時間を書き直す）
         fwrite(liveRate,       sizeof(double),   nChMax, ofp);   // Livetime の割合（最後に書き直す）
         fwrite(&nEvent,        sizeof(uint32_t), 1, ofp);   // イベント数を書き込む位置を確保（この時点では0なので、最後に書き直す）
      }
      capTime64 = (uint64_t)(capTime * 100000000); // s -> 10ns単位 に変換
      uint64_t timeStamp;
      uint16_t ped;
      switch (capMode) {
      case 0: // Oscilloscope Mode
      {
         uint16_t **data = new uint16_t* [nCh];
         for (uint16_t ic=0; ic<nCh; ic++) data[ic] = new uint16_t [capLength];

         /*
         // 別スレッドでキャプチャ状態を監視
         LiveTimeInfo lt;
         monitorCapture = true;
         std::thread monitorThread(monitorCaptureStatus, &lt);
         */

         // 別スレッドでトリガー発生数を監視
         TriggerMonitor trigMon;
         monitorTrigger = true;
         std::thread trigThread(monitorTriggerRate, &trigMon, std::cref(chs));

         elapsedTimeFPGA = 0;
         while (elapsedTimeFPGA < capTime64) {
            capState = csz_capture_status();

            if (capState == CAPSTAT_IDLE) { // キャプチャ状態が待機状態の場合
               capInfo = csz_capture_execute(chMask, capLength, CAPTRIG_NORMAL, 1); // オシロモードでキャプチャ実行

               if (capInfo.result == CAPRESULT_SUCCESS) { // キャプチャに成功した場合
                  timeStamp = capInfo.start_time48 - startTimeFPGA;
                  if (ofp) {
                     fwrite(&timeStamp, sizeof(uint64_t), 1, ofp);  // 計測開始からキャプチャまでのタイムスタンプ
                     fwrite(&capLength, sizeof(uint32_t), 1, ofp);  // キャプチャ長
                     //fwrite(&(capInfo.length), sizeof(int), 1, ofp);  // キャプチャ長
                     //fwrite(&(capInfo.chmask), sizeof(uint32_t), 1, ofp);  // キャプチャしたチャンネル
                     for (uint16_t ic=0; ic<nCh; ic++) {
                        nEventCh[chs[ic] - 1]++;
                        ped = csz_trig_pedestal(chs[ic]); // ペデスタル値取得
                        csz_capture_datacopy(capInfo, chs[ic], data[ic]); // メモリ上のデータコピー
                        fwrite(&ped,     sizeof(uint16_t), 1, ofp);         // ペデスタル値書き込み
                        fwrite(data[ic], sizeof(uint16_t), capLength, ofp); // 波形データ書き込み
                     }
                  }
                  nEvent++;
               }
            }
            elapsedTimeFPGA = csz_time48() - startTimeFPGA;
          //if (is_crtlc_pushed()) { cerr << "Aborted.\n";  break; }
            if (g_status == 1) throw tools::exception("main", "Capture(Oscilloscope Mode) Aborted.");
         }

         elapsedTimeFPGA = csz_time48() - startTimeFPGA;
         elapsedTime = (uint32_t)(elapsedTimeFPGA * 0.00001); // 10ns単位 -> ms に変換

         /*
         // 別スレッドで監視したキャプチャ状態のうち、トリガー待ち時間の割合を算出
         monitorCapture = false;
         monitorThread.join();
         double total = lt.idle + lt.trigwait + lt.run;
         if (total > 0.0) {
            std::cout
               << "\nLivetime ratio = " << lt.trigwait / total << "\n"
               << "	Idle     = " << lt.idle     << " s\n"
               << "	TrigWait = " << lt.trigwait << " s\n"
               << "	Run      = " << lt.run      << " s\n"
               << "	Total    = " << total       << " s\n";

            for (auto ch : chs) {
               liveRate[ch - 1] = lt.trigwait / total;
            }
         }
         */

         // 別スレッドで監視したトリガー発生数と実際に記録されたイベント数の割合を算出
         monitorTrigger = false;
         trigThread.join();
         for (auto ch : chs) {
            if (trigMon.nRate[ch - 1] > 0 && trigMon.triggerRate[ch - 1] > 0) {
               double t_rate = 1. * (trigMon.triggerRate[ch - 1] / trigMon.nRate[ch - 1]); // トリガーレート
               // 125 MSPSだけ時間の扱いが異なるよう(/s ではなく /0.8s)なので補正をかける（それでも若干少ないので、さらに1.03倍している）
               if (csz_adc_getfreq() == 125) t_rate *= 1.25 * 1.03;
               double e_rate = 1. * (nEventCh[ch - 1] / (elapsedTimeFPGA * 1.0e-8));       // イベントレート
               liveRate[ch - 1] = e_rate / t_rate;

               std::cout << "\nLivetime ratio ("<< ch <<") : " << liveRate[ch - 1] << "\n"
                  << "	Trigger Rate (/s) = " << t_rate << "\n"
                  << "	Event Rate (/s) = " << e_rate << "\n";
            }
         }

         for (uint16_t ic=0; ic<nCh; ic++) delete[] data[ic];
         delete[] data;

         break;
      }
      case 1: // Event Mode
      {
         // 別スレッドでトリガー発生数を監視
         TriggerMonitor trigMon;
         monitorTrigger = true;
         std::thread trigThread(monitorTriggerRate, &trigMon, std::cref(chs));

         elapsedTimeFPGA = 0;
         capInfo = csz_evcap_execute(chMask, capTime, CAPTRIG_MESUNIT, evcap, ofp);
         /*
         capInfo = csz_evcap_execute(chMask, capTime, CAPTRIG_MESUNIT, 0, 0);
         int length = (csz_reg_read(REGOFS_CAPTADDR) - capInfo.start_addr) / 2;
         nEvent += csz_reg_read(REGOFS_CAPTITER);
         unsigned short *map = (unsigned short *)csz_mem_vaddr(0);
         */

         elapsedTimeFPGA = csz_time48() - startTimeFPGA;
         elapsedTime = (uint32_t)(elapsedTimeFPGA * 0.00001); // 10ns単位 -> ms に変換

         // 別スレッドで監視したトリガー発生数と実際に記録されたイベント数の割合を算出
         monitorTrigger = false;
         trigThread.join();
         for (auto ch : chs) {
            if (trigMon.nRate[ch - 1] > 0 && trigMon.triggerRate[ch - 1] > 0) {
               double t_rate = 1. * (trigMon.triggerRate[ch - 1] / trigMon.nRate[ch - 1]); // トリガーレート
               // 125 MSPSだけ時間の扱いが異なるよう(/s ではなく /0.8s)なので補正をかける（それでも若干少ないので、さらに1.03倍している）
               if (csz_adc_getfreq() == 125) t_rate *= 1.25 * 1.03;
               double e_rate = 1. * (nEventCh[ch - 1] / (elapsedTimeFPGA * 1.0e-8));       // イベントレート
               liveRate[ch - 1] = e_rate / t_rate;

               std::cout << "\nLivetime ratio ("<< ch <<") : " << liveRate[ch - 1] << "\n"
                  << "	Trigger Rate (/s) = " << t_rate << "\n"
                  << "	Event Rate (/s) = " << e_rate << "\n";
            }
         }

         break;
      }
      default:
         throw tools::exception("main", "Capture mode must be 0 (Ocsilloscope mode) or 1 (Event mode).");
      }

      if (ofp) {
         fsetpos(ofp, &pos); // 最初に測定時間を書いた位置まで戻る
         fwrite(&elapsedTime, sizeof(uint32_t), 1, ofp);   // 測定時間 (ms)
         fwrite(&liveRate,    sizeof(double),   nChMax, ofp);   // Livetime の割合
         fwrite(&nEvent,      sizeof(uint32_t), 1, ofp);   // イベント数
         fclose(ofp);
      }

      csz_close();
      cout << "NEvent = " << nEvent << endl;
      cout << "Cosmo-Z closed." << endl;

      if (analysis) {
         string fileAnly = header + "_anly.dat";
         FILE *ofp2 = fopen(fileAnly.c_str(), "ab+");
         uint32_t nData = vData.size();
         fwrite(&wavelength,   sizeof(uint16_t), 1, ofp2);
         fwrite(&nData,        sizeof(uint32_t), 1, ofp2);
         fwrite(&elapsedTime,  sizeof(uint32_t), 1, ofp2);
         fwrite(&timeStampExt, sizeof(double),   1, ofp2);
         fwrite(&startTime,    4,                1, ofp2);
         fwrite(&capMode,      sizeof(uint16_t), 1, ofp2);
         fwrite(&chMask,       sizeof(uint32_t), 1, ofp2);
         fwrite(&capLength,    sizeof(uint32_t), 1, ofp2);
         for (int i=0; i<nData; i++) vData[i].Write(ofp2);
         cerr << "NEventAnalyzed = " << nData << endl;
         fclose(ofp2);
      }

      return 0;
   }
   catch (exception& err) {
      cerr << "exception : " << err.what() << endl;

      if (ofp) {
         fsetpos(ofp, &pos); // 最初に測定時間を書いた位置まで戻る
         fwrite(&elapsedTime, sizeof(uint32_t), 1, ofp);   // 測定時間 (ms)
         fwrite(&liveRate,    sizeof(double),   nChMax, ofp);   // Livetime の割合
         fwrite(&nEvent,      sizeof(uint32_t), 1, ofp);   // イベント数
         fclose(ofp);
      }

      csz_close();
      cout << "NEvent = " << nEvent << endl;
      cout << "Cosmo-Z closed." << endl;

      return 1;
   }
}

