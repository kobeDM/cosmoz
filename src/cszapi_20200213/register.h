/*
 * register.h
 *
 *  Created on: 2017/10/11
 *      Author: user
 */

#ifndef SRC_REGISTER_H_
#define SRC_REGISTER_H_



#define REGOFS_IREG0    0  //!< FPGAの内蔵レジスタ0 いろいろな機能があるが、そのうち廃止される
#define REGOFS_FPGAV    1  //!< FPGAの内蔵レジスタ1 FPGAのバージョン番号が入るレジスタ。そのうち廃止
#define REGOFS_SPIDATA  2  //!< FPGAの内蔵レジスタ2 FPGAがSPI通信するためのレジスタ
#define REGOFS_SPISTAT  3  //!< FPGAの内蔵レジスタ3
#define REGOFS_CALSTAT  4  //!< FPGAの内蔵レジスタ4
#define REGOFS_CALSTAT2 5  //!< FPGAの内蔵レジスタ5
#define REGOFS_TIME48   6  //!< FPGAの内蔵レジスタ6

#define REGOFS_INTCSR   7  //!< FPGAの内蔵レジスタ7
#define REGOFS_ADCCMD   8  //!< FPGAの内蔵レジスタ8
#define REGOFS_RDWRCH   9  //!< FPGAの内蔵レジスタ9
#define REGOFS_RAWADC   10  //!< FPGAの内蔵レジスタ10
#define REGOFS_ADCSEL   11  //!< FPGAの内蔵レジスタ11
#define REGOFS_MULOFS   12  //!< FPGAの内蔵レジスタ12
#define REGOFS_ADCDELAY 13  //!< FPGAの内蔵レジスタ13
#define REGOFS_TRIGINFO 14  //!< FPGAの内蔵レジスタ14
#define REGOFS_TRIGCTRL 15  //!< FPGAの内蔵レジスタ15

#define REGOFS_CAPTLEN   16  //!< FPGAの内蔵レジスタ16
#define REGOFS_CAPTCH    17  //!< FPGAの内蔵レジスタ17
#define REGOFS_CAPTADDR  18  //!< FPGAの内蔵レジスタ18
#define REGOFS_CAPTTIME  19  //!< FPGAの内蔵レジスタ19
#define REGOFS_CAPTCTRL  20  //!< FPGAの内蔵レジスタ20
#define REGOFS_CAPTCOUNT 21  //!< FPGAの内蔵レジスタ21
#define REGOFS_CAPTDLY   22  //!< FPGAの内蔵レジスタ22
#define REGOFS_CAPTITER  23  //!< FPGAの内蔵レジスタ23
#define REGOFS_CAPTBADDR 24  //!< FPGAの内蔵レジスタ24

#define REGOFS_PEDESTAL  32  //!< FPGAの内蔵レジスタ32
#define REGOFS_CPS       33  //!< FPGAの内蔵レジスタ33
#define REGOFS_PLSHI     34  //!< FPGAの内蔵レジスタ34
#define REGOFS_MCA       35  //!< FPGAの内蔵レジスタ35

#define REGOFS_XADCTEMP  40  //!< FPGAの内蔵レジスタ0
#define REGOFS_XADCVCCI  41  //!< FPGAの内蔵レジスタ0
#define REGOFS_XADCVCCA  42  //!< FPGAの内蔵レジスタ0
#define REGOFS_XADCVDDR  43  //!< FPGAの内蔵レジスタ0

#define REGOFS_GPSSEC    44  //!< FPGAの内蔵レジスタ0
#define REGOFS_GPS10NS   45  //!< FPGAの内蔵レジスタ0
#define REGOFS_GPSMAX    46  //!< FPGAの内蔵レジスタ0
#define REGOFS_GPSCAPSEC 47  //!< FPGAの内蔵レジスタ0
#define REGOFS_GPSCAP10N 48  //!< FPGAの内蔵レジスタ0
#define REGOFS_ADCCLKADJ 62  //!< FPGAの内蔵レジスタ49

#define FLAG_CAPTURE_START (1 << 0)
#define FLAG_CAPTURE_AUTO  (1 << 1)
#define FLAG_CAPTURE_RUN   (1 << 2)
#define FLAG_CAPTURE_ERROR (1 << 3)
#define FLAG_CAPTURE_DONE  (1 << 4)

#endif /* SRC_REGISTER_H_ */
