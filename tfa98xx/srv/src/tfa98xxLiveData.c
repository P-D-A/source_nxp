/*
 *Copyright 2014 NXP Semiconductors
 *
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *
 *http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *See the License for the specific language governing permissions and
 *limitations under the License.
 */


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include "dbgprint.h"
#include "Tfa98API.h"
#include "Tfa98xx.h"
#include "nxpTfa98xx.h"
#include "tfa98xxLiveData.h"
#include "tfa98xxRuntime.h"
#include "tfaContainer.h"

// globals
int tfa98xxLiveData_trace = 1;
int tfa98xxLiveData_verbose = 0;

static int maxdev = 0;
static unsigned char tfa98xxI2cbase;    // global for i2c access
static FILE *pFile;

#define I2C(idx) ((tfa98xxI2cbase+idx)*2)

void LiveData_trace(int level) {
    tfa98xxLiveData_trace = level;
}

/*
 * Set the debug option
 */
void tfaLiveDataVerbose(int level) {
    tfa98xxLiveData_verbose = level;
}

/*maxdev
 * set the I2C slave base address and the nr of consecutive devices
 * the devices will be opened
 * TODO check for conflicts if already open
 */
tfa_srv_api_error_t nxpTfa98xxOpenLiveDataSlaves(Tfa98xx_handle_t *handlesIn, int i2cbase, int maxdevices)
{
        Tfa98xx_Error_t err;
        int i;
        maxdev = maxdevices;

        if ( (maxdev < 1) | (maxdev > 4) )    // only allow one 1 up to 4
                return tfa_srv_api_error_BadParam;

        tfa98xxI2cbase = i2cbase;       // global for i2c access

        for (i = 0; i < maxdev; i++)
        {
           tfaContGetSlave(i, &tfa98xxI2cSlave); /* get device I2C address */
           if( handlesIn[i] == -1)
           {
              //err = Tfa98xx_Open(I2C(i), &handlesIn[i] );
              err = Tfa98xx_Open(tfa98xxI2cSlave*2, &handlesIn[i]);
              PRINT_ASSERT( err);
           }
        }

        return err;
}

/*
 * return the structure with the required data record
 *  When Speaker Damage is detected  the error code will reflect this.
 */
tfa_srv_api_error_t nxpTfa98xxGetLiveData( Tfa98xx_handle_t *handlesIn,
                                          int idx,
                                          nxpTfa98xx_LiveData_t * record)
{
        Tfa98xx_Error_t err;
        Tfa98xx_StateInfo_t stateInfo;
        unsigned char inforegs[6];
        short icTemp_2Complement;

        err = Tfa98xx_DspGetStateInfo(handlesIn[idx], &stateInfo);
        if ( err == Tfa98xx_Error_DSP_not_running)
            return err;
        assert(err == Tfa98xx_Error_Ok);

        if (err == Tfa98xx_Error_Ok) {
                if (tfa98xxLiveData_verbose)
                        dump_state_info(&stateInfo);
        }
        record->statusFlags = stateInfo.statusFlag;
        record->limitClip = stateInfo.sMax;     /* Current Clip/Lim threshold */
        record->agcGain = stateInfo.agcGain;    /* Current AGC Gain value */
        record->limitGain = stateInfo.limGain;
        record->speakerTemp = stateInfo.T;
        record->boostExcursion = stateInfo.X1;  /* Current estimated Excursion value caused by Speakerboost gain control */
           record->manualExcursion = stateInfo.X2; /* obsolete */
        record->speakerResistance = stateInfo.Re;
        record->shortOnMips = stateInfo.shortOnMips;
        // get regs, and check status
        err = Tfa98xx_ReadData(handlesIn[idx], 0, 6, inforegs);
        assert(err == Tfa98xx_Error_Ok);
        //
        record->statusRegister = inforegs[0] << 8 | inforegs[1];

        record->batteryVoltage = (float)(inforegs[2] << 8 | inforegs[3]);
        record->batteryVoltage = (float)((record->batteryVoltage * 5.5)/1024);

        icTemp_2Complement = inforegs[4] << 8 | inforegs[5];
        record->icTemp = (icTemp_2Complement >= 256) ? (icTemp_2Complement - 512) : icTemp_2Complement;

        /*
         * check system sanity and take action if required
         * this may cause a forced reload
         */
        if (tfaRunCheckAlgo(handlesIn[idx]))
        {
         PRINT("statusRegister: 0x%02x\n", record->statusRegister);
             tfaRunSpeakerBoost(handlesIn[idx], 1); //force a reload
             return tfa_srv_api_error_Fail;
        }

        tfaRunResolveIncident(handlesIn[idx], tfaRunCheckEvents(record->statusRegister));

    return err;
}

/*
 * return the structure with the required data record
 *  When Speaker Damage is detected  the error code will reflect this.
 */
tfa_srv_api_error_t nxpTfa98xxDummyDataInit(void)
{
   char line[256];
   pFile = fopen("record.csv","r");

   if (pFile == NULL)
   {
      return tfa_srv_api_error_BadParam;
   }

  /*skip the 1st line*/
  if ( fgets( line , sizeof(line), pFile ) == NULL ) {
    return tfa_srv_api_error_Fail;
  }

   return tfa_srv_api_error_Ok;
}

/*
 * return the structure with the required data record
 *  When Speaker Damage is detected  the error code will reflect this.
 */
tfa_srv_api_error_t nxpTfa98xxGetDummyData(nxpTfa98xx_LiveData_t * record )
{
  char line[256];
  int n = 0;
    float agcGain = 0;
  float limitGain = 0;
  float batteryVoltage = 0,limitClip = 0;
    float boostExcursion = 0,manualExcursion = 0,speakerResistance = 0;
    unsigned int  icTemp = 0;
    unsigned int speakerTemp = 0,shortOnMips=0;
    unsigned short statusRegister = 0, statusFlags = 0;

   /*skip the 1st line if end of file*/
    if ( feof(pFile) ) {
        rewind(pFile);
        if ( fgets( line , sizeof(line), pFile ) == NULL ) {
      return tfa_srv_api_error_Fail;
    }
    }

  if ( fgets( line , sizeof(line), pFile ) == NULL ) {
    return tfa_srv_api_error_Fail;
  }
    n = sscanf(line, "%hx,0x%4hx,%f,%f,%f,%f,%d,%d,%f,%f,%f,%d",
            &statusRegister,
            &statusFlags, &agcGain, &limitGain, &limitClip,
            &batteryVoltage, &speakerTemp, &icTemp, &boostExcursion,
            &manualExcursion, &speakerResistance, &shortOnMips );
   if (n == 11)
   {
      record->statusFlags = statusFlags;
      record->limitClip = limitClip;     /* Current Clip/Lim threshold */
      record->agcGain = agcGain;    /* Current AGC Gain value */
      record->limitGain = limitGain;
      record->speakerTemp = speakerTemp;
      /* Current estimated Excursion value caused by Speakerboost gain control */
      record->boostExcursion = boostExcursion;
      record->manualExcursion = manualExcursion;
      record->speakerResistance = speakerResistance;
      record->statusRegister = statusRegister;
      record->batteryVoltage = batteryVoltage;
      record->icTemp = icTemp;
      record->shortOnMips = shortOnMips;
   }
   else
   {
      return tfa_srv_api_error_Fail;
   }
   if ( record->DrcSupport){
       PRINT("hiero nog drc!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
   }
   return tfa_srv_api_error_Ok;
}

/*
 * run for the amount of specified intervals (in milliseconds) and return the recorded data.
 *   (block until ready: count*msInterval msecs)
 */
tfa_srv_api_error_t nxpTfa98xxLogSpeakerStateInfo(int msInterval, int count,
                                                 nxpTfa98xx_LiveData_t **
                                                 record)
{
        return 1;
}

/*
 * call the recordSpeakerStateInfoCallback function every msInterval with the recorded data
 *  (this will start a thread and will stop when the callback returns 0)
 */
tfa_srv_api_error_t nxpTfa98xxSendSpeakerStateInfo(int msInterval,
                                                  int
                                                  (*recordSpeakerStateInfoCallback)
                                                  (nxpTfa98xx_LiveData_t *
                                                   record))
{
        return 1;
}

static char *stateFlagsStr(int stateFlags)
{
        static char flags[10];

        flags[0] =
            (stateFlags & (0x1 << Tfa98xx_SpeakerBoost_Activity)) ? 'A' : 'a';
        flags[1] =
            (stateFlags & (0x1 << Tfa98xx_SpeakerBoost_S_Ctrl)) ? 'S' : 's';
        flags[2] =
            (stateFlags & (0x1 << Tfa98xx_SpeakerBoost_Muted)) ? 'M' : 'm';
        flags[3] =
            (stateFlags & (0x1 << Tfa98xx_SpeakerBoost_X_Ctrl)) ? 'X' : 'x';
        flags[4] =
            (stateFlags & (0x1 << Tfa98xx_SpeakerBoost_T_Ctrl)) ? 'T' : 't';
        flags[5] =
            (stateFlags & (0x1 << Tfa98xx_SpeakerBoost_NewModel)) ? 'L' : 'l';
        flags[6] =
            (stateFlags & (0x1 << Tfa98xx_SpeakerBoost_VolumeRdy)) ? 'V' : 'v';
        flags[7] =
            (stateFlags & (0x1 << Tfa98xx_SpeakerBoost_Damaged)) ? 'D' : 'd';
        flags[8] =
            (stateFlags & (0x1 << Tfa98xx_SpeakerBoost_SignalClipping)) ? 'C' :
            'c';

        flags[9] = 0;
        return flags;
}

void dump_state_info(Tfa98xx_StateInfo_t * pState)
{
/**
#if !(defined(WIN32) || defined(_X64))
        PRINT
            ("state: flags %s, agcGain %2.1llu\tlimGain %2.1llu\tsMax %2.1llu\tT %d\tX1 %2.1llu\tRe %2.2llu\tshortOnMips %d\n",
             stateFlagsStr(pState->statusFlag), pState->agcGain,
             pState->limGain, pState->sMax, pState->T, pState->X1, pState->Re, pState->shortOnMips);
#else
**/
        PRINT
            ("state: flags %s, agcGain %2.1f\tlimGain %2.1f\tsMax %2.1f\tT %d\tX1 %2.1f\tRe %2.2f\tshortOnMips %d\n",
             stateFlagsStr(pState->statusFlag), pState->agcGain,
             pState->limGain, pState->sMax, pState->T, pState->X1, pState->Re, pState->shortOnMips);
//#endif

}

void tfa98xxPrintRecordHeader(Tfa98xx_handle_t *handlesIn, char *csv, int deltatime)
{
    int DrcSupport;
    FILE *outfile;

    if (csv && csv[0] != '\0' && strcmp(csv,"stdout"))
        outfile = fopen(csv, "w");
    else
        outfile = stdout;

    PRINT_FILE(outfile, "recording interval time: %d ms\n", deltatime);
    PRINT_FILE(outfile, "line,i2caddr,statusRegister,"
            "statusFlags,"
            "agcGain,"
            "limitGain,"
            "limitClip,"
            "batteryVoltage,"
            "speakerTemp,"
            "icTemp,"
            "boostExcursion,"
            "manualExcursion,"
            "speakerResistance,"
            "shortOnMips");

    PRINT_FILE(outfile,         "\n");

    if (csv && csv[0] != '\0' && strcmp(csv,"stdout"))
        fclose(outfile);
}

/*
 *  data logger for life time testing
 */
typedef enum
{
    tfaLeft,
    tfaRight
} side_t;
typedef enum
{
    tfaZmodel,
    tfaXmodel
} model_t;
#define MODELBUFSIZE (423+5) //32bits linenr + 0x55 + modeldata
static void tfa98xxLogModel(Tfa98xx_handle_t *handlesIn, side_t idx,
            int currentLine, FILE *fp, model_t type)
{
    int actual;
   unsigned char buffer[MODELBUFSIZE];
    *(unsigned int*)buffer= currentLine;

    buffer[4] = 0x55; //marker

    tfa98xxGetRawSpeakerModel(handlesIn, &buffer[5], type, idx); //X=1 Z=0
    actual = (int)(fwrite( buffer, 1, sizeof(buffer), fp));
    assert(actual==MODELBUFSIZE);

}
static int tfa98xxLogStateLine(Tfa98xx_handle_t *handlesIn, int devIdx, int currentline, unsigned char i2cAdr, char *csv)
{
    Tfa98xx_Error_t err;
    int DrcSupport;
    nxpTfa98xx_LiveData_t record;
    FILE *outfile;

    if (csv && csv[0] != '\0' && strcmp(csv,"stdout"))
        outfile = fopen(csv, "a");
    else
        outfile = stdout;

    err = nxpTfa98xxGetLiveData(handlesIn, devIdx, &record);
    if (err!=Tfa98xx_Error_Ok) {
        PRINT_ERROR("No live data! (is DSP running?)\n");
        exit(1);
    }
    PRINT_FILE(outfile, "%d,0x%02x,0x%04x,0x%04x,%f,%f,%f,%f,%d,%d,%f,%f,%f,%d",
            currentline,             //d
            i2cAdr,                     //d
            record.statusRegister,   //x
            record.statusFlags,      //x Masked bit word, see Tfa98xxStatusFlags
            record.agcGain,             //f Current AGC Gain value
            record.limitGain,        //f Current Limiter Gain value
            record.limitClip,        //f Current Clip/Lim threshold
            record.batteryVoltage,   //d
            record.speakerTemp,      //d Current Speaker Temperature value
            record.icTemp,           //d Current ic/die Temperature value
            record.boostExcursion,   //f Current estimated Excursion value caused by Speakerboost gain control
            record.manualExcursion,  //f Current estimated Excursion value caused by manual gain setting
            record.speakerResistance,//f Current Loudspeaker blocked resistance
            record.shortOnMips         //d MIPS problem detected
        );
    PRINT_FILE(outfile, "\n");

    if (csv && csv[0] != '\0' && strcmp(csv,"stdout"))
        fclose(outfile);

    return 0;
}

/*
 * stateinfo: 01L_0000.CSV 01R_0000.CSV
 * xmodels:      01L_XMDL.BIN 01R_XMDL.BIN
 * zmodels:      01L_ZMDL.BIN 01R_ZMDL.BIN
 */
#define MODELLOGINTERVAL (30)
#define MAX_DEVS 1 //TODO use cmdline --speaker to specify more devs to log
int tfa98xxLogger( int interval, int loopcount)
{
    Tfa98xx_handle_t handlesIn[] ={-1, -1};
    int modelinterval = interval*MODELLOGINTERVAL;
    int currentline=1, modelcount=0, i, j;

    // logfiles
    FILE *out[2*MAX_DEVS], *fp[3*MAX_DEVS];
    char *fname[]={ "01L_0000.CSV" ,"01L_XMDL.BIN","01L_ZMDL.BIN" ,
                     "01R_0000.CSV", "01R_XMDL.BIN","01R_ZMDL.BIN" };

    /*
     * print info:
     *   state info interval = interval secs
     *   total lines = loopcount
     *   total time = loopcount*interval secs /60=mins/3600=hours
     *   model log time = every MODELLOGINTERVAL lines = modelinterval secs
     *
     */
    out[0] = stdout;
    out[1] = fopen("RUN.LOG", "a");
    for (i=0;i<MAX_DEVS;i++) {
        PRINT_FILE(out[i], "data logger starting:\n");
        PRINT_FILE(out[i], " state info interval = %d seconds\n", interval);
        PRINT_FILE(out[i], " total lines = %d\n", loopcount);
        PRINT_FILE(out[i], " total time = %d seconds = %.2f minutes = %.2f hours\n",
                loopcount*interval, (float)(loopcount*interval)/60, (float)(loopcount*interval)/3600);
        PRINT_FILE(out[i], " model log time every %d lines = %d seconds\n",
                MODELLOGINTERVAL ,modelinterval);
        PRINT_FILE(out[i], " files:");
        for (j=0; j<3*MAX_DEVS; j++) {
            PRINT_FILE(out[i]," %s", fname[j]);
        }
        PRINT_FILE(out[i], "\n");

        // open files
        for (i=0; i<3*MAX_DEVS; i++) {
            fp[i] = fopen( fname[i], "a+b");
            if ( fp[i] == NULL) {
                PRINT_ERROR("can't open logfile:%s\n", fname[i]);
                return 0;
            }
        }
    }
    fflush(out[1]);
    fclose(out[1]);

    if (  tfa_srv_api_error_Ok == nxpTfa98xxOpenLiveDataSlaves(handlesIn, tfa98xxI2cSlave, MAX_DEVS))
        do {
            tfa98xxLogStateLine(handlesIn, tfaLeft, currentline, tfa98xxI2cSlave, (char *)fp[0]);// left
#if MAX_DEVS>1
            tfa98xxLogStateLine(handlesIn, tfaRight, currentline, fp[3]);// right
#endif
            if (currentline > modelinterval*modelcount) {
                //do model
                modelcount++;
                //
                tfa98xxLogModel( handlesIn, tfaLeft,  currentline, fp[1], tfaZmodel);
                tfa98xxLogModel( handlesIn, tfaLeft,  currentline, fp[2], tfaXmodel);
#if MAX_DEVS>1
                tfa98xxLogModel( handlesIn, tfaRight, currentline, fp[4], tfaZmodel);
                tfa98xxLogModel( handlesIn, tfaRight, currentline, fp[5], tfaXmodel);
#endif
                PRINT("line:%d, model:%d\n", currentline, modelcount);
            } else PRINT("line:%d\n", currentline);
            currentline++;
            loopcount = ( loopcount == 0) ? 1 : loopcount-1 ;
            tfaRun_Sleepus(interval*1000000); // is seconds interval
        } while (loopcount>0) ;

    // close files
    for (i=0; i<3*MAX_DEVS; i++)
        fclose(fp[i]);

    return currentline;

}

void tfa98xxPrintRecord(Tfa98xx_handle_t *handlesIn, int devIdx, char *csv, unsigned char i2cAddrs)
{
        static int currentline=1;
        tfa98xxLogStateLine(handlesIn, devIdx, currentline++, i2cAddrs, csv);
}


typedef float fix;
#define MIN(a,b) ((a)<(b)?(a):(b))
static void convertData2Bytes(int num_data, const int data[],
                              unsigned char bytes[])
{
        int i;                  /* index for data */
        int k;                  /* index for bytes */
        int d;

        /* note: cannot just take the lowest 3 bytes from the 32 bit integer, because also need to take care of clipping any value > 2&23 */
        for (i = 0, k = 0; i < num_data; ++i, k += 3) {
                if (data[i] >= 0) {
                        d = MIN(data[i], (1 << 23) - 1);
                } else {
                        d = (1 << 24) - MIN(-data[i], 1 << 23); /* 2's complement */
                }
                assert(d >= 0);
                assert(d < (1 << 24));  /* max 24 bits in use */
                bytes[k] = (d >> 16) & 0xFF;    /* MSB */
                bytes[k + 1] = (d >> 8) & 0xFF;
                bytes[k + 2] = (d) & 0xFF;      /* LSB */
        }
}

/* convert DSP memory bytes to signed 24 bit integers
   data contains "num_bytes/3" elements
   bytes contains "num_bytes" elements */
static void convertBytes2Data(int num_bytes, const unsigned char bytes[],
                              int data[])
{
        int i;                  /* index for data */
        int k;                  /* index for bytes */
        int d;
        int num_data = num_bytes / 3;

        assert((num_bytes % 3) == 0);
        for (i = 0, k = 0; i < num_data; ++i, k += 3) {
                d = (bytes[k] << 16) | (bytes[k + 1] << 8) | (bytes[k + 2]);
                assert(d >= 0);
                assert(d < (1 << 24));  /* max 24 bits in use */
                if (bytes[k] & 0x80) {  /* sign bit was set */
                        d = -((1 << 24) - d);
                }
                data[i] = d;
        }
}

/////////////////////////////////////////////////////////
// Sorensen in-place split-radix FFT for real values
// data: array of doubles:
// re(0),re(1),re(2),...,re(size-1)
//
// output:
// re(0),re(1),re(2),...,re(size/2),im(size/2-1),...,im(1)
// normalized by array length
//
// Source:
// Sorensen et al: Real-Valued Fast Fourier Transform Algorithms,
// IEEE Trans. ASSP, ASSP-35, No. 6, June 1987
void realfft_split(double *data, long n)
{
        long i, j, k, i5, i6, i7, i8, i0, id, i1, i2, i3, i4, n2, n4, n8;
        double t1, t2, t3, t4, t5, t6, a3, ss1, ss3, cc1, cc3, a, e, sqrt2;
        float pi = 3.1415926f;
        sqrt2 = sqrt(2.0);
        n4 = n - 1;
//data shuffling
        for (i = 0, j = 0, n2 = n / 2; i < n4; i++) {
                if (i < j) {
                        t1 = data[j];
                        data[j] = data[i];
                        data[i] = t1;
                }
                k = n2;
                while (k <= j) {
                        j -= k;
                        k >>= 1;
                }
                j += k;
        }
    /*----------------------*/
//length two butterflies
        i0 = 0;
        id = 4;
        do {
                for (; i0 < n4; i0 += id) {
                        i1 = i0 + 1;
                        t1 = data[i0];
                        data[i0] = t1 + data[i1];
                        data[i1] = t1 - data[i1];
                }
                id <<= 1;
                i0 = id - 2;
                id <<= 1;
        } while (i0 < n4);
    /*----------------------*/
//L shaped butterflies
        n2 = 2;
        for (k = n; k > 2; k >>= 1) {
                n2 <<= 1;
                n4 = n2 >> 2;
                n8 = n2 >> 3;
                e = 2 * pi / (n2);
                i1 = 0;
                id = n2 << 1;
                do {
                        for (; i1 < n; i1 += id) {
                                i2 = i1 + n4;
                                i3 = i2 + n4;
                                i4 = i3 + n4;
                                t1 = data[i4] + data[i3];
                                data[i4] -= data[i3];
                                data[i3] = data[i1] - t1;
                                data[i1] += t1;
                                if (n4 != 1) {
                                        i0 = i1 + n8;
                                        i2 += n8;
                                        i3 += n8;
                                        i4 += n8;
                                        t1 = (data[i3] + data[i4]) / sqrt2;
                                        t2 = (data[i3] - data[i4]) / sqrt2;
                                        data[i4] = data[i2] - t1;
                                        data[i3] = -data[i2] - t1;
                                        data[i2] = data[i0] - t2;
                                        data[i0] += t2;
                                }
                        }
                        id <<= 1;
                        i1 = id - n2;
                        id <<= 1;
                } while (i1 < n);
                a = e;
                for (j = 2; j <= n8; j++) {
                        a3 = 3 * a;
                        cc1 = cos(a);
                        ss1 = sin(a);
                        cc3 = cos(a3);
                        ss3 = sin(a3);
                        a = j * e;
                        i = 0;
                        id = n2 << 1;
                        do {
                                for (; i < n; i += id) {
                                        i1 = i + j - 1;
                                        i2 = i1 + n4;
                                        i3 = i2 + n4;
                                        i4 = i3 + n4;
                                        i5 = i + n4 - j + 1;
                                        i6 = i5 + n4;
                                        i7 = i6 + n4;
                                        i8 = i7 + n4;
                                        t1 = data[i3] * cc1 + data[i7] * ss1;
                                        t2 = data[i7] * cc1 - data[i3] * ss1;
                                        t3 = data[i4] * cc3 + data[i8] * ss3;
                                        t4 = data[i8] * cc3 - data[i4] * ss3;
                                        t5 = t1 + t3;
                                        t6 = t2 + t4;
                                        t3 = t1 - t3;
                                        t4 = t2 - t4;
                                        t2 = data[i6] + t6;
                                        data[i3] = t6 - data[i6];
                                        data[i8] = t2;
                                        t2 = data[i2] - t3;
                                        data[i7] = -data[i2] - t3;
                                        data[i4] = t2;
                                        t1 = data[i1] + t5;
                                        data[i6] = data[i1] - t5;
                                        data[i1] = t1;
                                        t1 = data[i5] + t4;
                                        data[i5] -= t4;
                                        data[i2] = t1;
                                }
                                id <<= 1;
                                i = id - n2;
                                id <<= 1;
                        } while (i < n);
                }
        }
// energy component is disabled
//division with array length
        // for (i = 0; i < n; i++)
        //     data[i] /= n;
}

void leakageFilter(double *data, int cnt, double lf)
{
        int i;
        double phi;
        float pi = 3.1415926f;

        for (i = 0; i < cnt; i++) {
                phi = sin(0.5 * pi * i / (cnt));
                phi *= phi;
                data[i] = sqrt(pow(1 + lf, 2) - 4 * lf * phi);
        }
}

/**
* Untangle the real and imaginary parts and take the modulus
*/
void untangle(double *waveform, double *hAbs, int L)
{
        int i, cnt = L / 2;

        hAbs[0] =
            sqrt((waveform[0] * waveform[0]) +
                 (waveform[L / 2] * waveform[L / 2]));
        for (i = 1; i < cnt; i++)
                hAbs[i] =
                    sqrt((waveform[i] * waveform[i]) +
                         (waveform[L - i] * waveform[L - i]));

}

/**
* Untangle the real and imaginary parts and take the modulus
*  and divide by the leakage filter
*/
void untangle_leakage(double *waveform, double *hAbs, int L, double leakage)
{
        int i, cnt = L / 2;
        double m_Aw[64];

        // create the filter
        leakageFilter(m_Aw, cnt, leakage);
        //
        hAbs[0] =
            sqrt((waveform[0] * waveform[0]) +
                 (waveform[L / 2] * waveform[L / 2])) / m_Aw[0];
        for (i = 1; i < cnt; i++)
                hAbs[i] =
                    sqrt((waveform[i] * waveform[i]) +
                         (waveform[L - i] * waveform[L - i])) / m_Aw[i];
}

#define PARAM_GET_LSMODEL           0x86        // Gets current LoudSpeaker impedance Model.
#define PARAM_GET_LSMODELW            0xC1        // Gets current LoudSpeaker xcursion Model.
int nxpTfa98xxGetSpeakerModel(   Tfa98xx_handle_t *handlesIn,
                                 int xmodel,
                                 SPKRBST_SpkrModel_t *record,
                                 double waveformData[65],
                                 float frequency[65],
                                 int idx )
{
   Tfa98xx_Error_t error = Tfa98xx_Error_Ok;
   unsigned char bytes[3 * 141];
   int data[141];
   int i = 0;
   double fft[128], untangled[128];

   error = Tfa98xx_DspGetParam(handlesIn[idx], 1,
                               xmodel ? PARAM_GET_LSMODELW :
                               PARAM_GET_LSMODEL, 423, bytes);
   assert(error == Tfa98xx_Error_Ok);

   convertBytes2Data(sizeof(bytes), bytes, data);

   for (i = 0; i < 128; i++)
   {
      /*record->pFIR[i] = (double)data[i] / ((1 << 23) * 2);*/
      record->pFIR[i] = (double)data[i] / (1 << 22);
   }

   record->Shift_FIR = data[i++];   ///< Exponent of HX data
   record->leakageFactor = (float)data[i++] / (1 << (23));  ///< Excursion model integration leakage
   record->ReCorrection = (float)data[i++] / (1 << (23));   ///< Correction factor for Re
   record->xInitMargin = (float)data[i++] / (1 << (23 - 2));        ///< (can change) Margin on excursion model during startup
   record->xDamageMargin = (float)data[i++] / (1 << (23 - 2));      ///< Margin on excursion modelwhen damage has been detected
   record->xMargin = (float)data[i++] / (1 << (23 - 2));    ///< Margin on excursion model activated when LookaHead is 0
   record->Bl = (float)data[i++] / (1 << (23 - 2)); ///< Loudspeaker force factor
   record->fRes = data[i++];        ///< (can change) Estimated Speaker Resonance Compensation Filter cutoff frequency
   record->fResInit = data[i++];    ///< Initial Speaker Resonance Compensation Filter cutoff frequency
   record->Qt = (float)data[i++] / (1 << (23 - 6)); ///< Speaker Resonance Compensation Filter Q-factor
   record->xMax = (float)data[i++] / (1 << (23 - 7));       ///< Maximum excursion of the speaker membrane
   record->tMax = (float)data[i++] / (1 << (23 - 9));       ///< Maximum Temperature of the speaker coil
   record->tCoefA = (float)data[i++] / (1 << 23);   ///< (can change) Temperature coefficient

   if (xmodel)
   {
      //xmodel 0xc1
      for (i = 0; i < 128; i++)
      {
         fft[127 - i] = (double)data[i] / (1 << 22);
      }
      realfft_split(fft, 128);
      untangle_leakage(fft, untangled, 128, -record->leakageFactor);
      for (i = 0; i < 128 / 2; i++)
      {
         waveformData[i] = 2 * untangled[i];
      }
   }
   else
   {
      //zmodel 0x86
      for (i = 0; i < 128; i++)
      {
         fft[i] = (double)data[i] / (1 << 22);
      }
      realfft_split(fft, 128);
      untangle(fft, untangled, 128);
      for (i = 0; i < 128 / 2; i++)
      {
         waveformData[i] = 1 / untangled[i];
      }
   }

   frequency[0] = 0;
   for (i = 1; i < 65; i++)
   {
      frequency[i] = (float)(frequency[i-1] + 62.5);
   }

   return error;
}

int nxpTfa98xxSetSpeakerModel(   Tfa98xx_handle_t *handlesIn,
                                 SPKRBST_SpkrModel_t *record,
                                 int idx )
{
   Tfa98xx_Error_t error = Tfa98xx_Error_Ok;
   unsigned char bytes[3 * 141];
   int data[141];
   int i = 0;

   assert(error == Tfa98xx_Error_Ok);

   for (i = 0; i < 128; i++)
   {
      data[i] = (int)(record->pFIR[i] * (1 << 22));
   }

   data[i++] = record->Shift_FIR; /*Exponent of HX data*/
   data[i++] = (int)(record->leakageFactor * (1 << (23))); /*Excursion model integration leakage*/
   data[i++] = (int)(record->ReCorrection * (1 << (23))); /*Correction factor for Re*/
   data[i++] = (int)(record->xInitMargin * (1 << (23 - 2))); /*(can change) Margin on excursion model during startup*/
   data[i++] = (int)(record->xDamageMargin * (1 << (23 - 2))); /*Margin on excursion modelwhen damage has been detected*/
   data[i++] = (int)(record->xMargin * (1 << (23 - 2))); /*Margin on excursion model activated when LookaHead is 0*/
   data[i++] = (int)(record->Bl * (1 << (23 - 2))); /*Loudspeaker force factor*/
   data[i++] = record->fRes; /*(can change) Estimated Speaker Resonance Compensation Filter cutoff frequency*/
   data[i++] = record->fResInit; /*Initial Speaker Resonance Compensation Filter cutoff frequency*/
   data[i++] = (int)(record->Qt * (1 << (23 - 6))); /*Speaker Resonance Compensation Filter Q-factor*/
   data[i++] = (int)(record->xMax * (1 << (23 - 7))); /*Maximum excursion of the speaker membrane*/
   data[i++] = (int)(record->tMax * (1 << (23 - 9))); /*Maximum Temperature of the speaker coil*/
   data[i++] = (int)(record->tCoefA * (1 << 23)); /*(can change) Temperature coefficient*/

   convertData2Bytes(141, data, bytes);

   error = Tfa98xx_DspWriteSpeakerParameters(handlesIn[idx], TFA98XX_SPEAKERPARAMETER_LENGTH, (unsigned char*) bytes);

   return error;
}

void tfa98xxGetRawSpeakerModel(Tfa98xx_handle_t *handlesIn,
                            unsigned char *buffer, int xmodel, int idx)
{
    Tfa98xx_Error_t error = Tfa98xx_Error_Ok;

    error = Tfa98xx_DspGetParam(handlesIn[idx], 1,
                                xmodel ? PARAM_GET_LSMODELW : PARAM_GET_LSMODEL,
                                423, buffer);
    assert(error == Tfa98xx_Error_Ok);

}

int tfa98xxPrintSpeakerModel(Tfa98xx_handle_t *handlesIn, char *csv, int xmodel, int idx, int maxdev)
{
        Tfa98xx_Error_t error = Tfa98xx_Error_Ok;
        SPKRBST_SpkrModel_t record;
        unsigned char bytes[3 * 141];
        int data[141];
        int i, hz;
        double fft[128], waveform[128], untangled[128];
        FILE *outfile;

        if (csv && csv[0] != '\0' && strcmp(csv,"stdout"))
            outfile = fopen(csv, "w");
        else
            outfile = stdout;

        error = Tfa98xx_DspGetParam(handlesIn[idx], 1, xmodel ? PARAM_GET_LSMODELW : PARAM_GET_LSMODEL, 423, bytes);

        if (error != Tfa98xx_Error_Ok)
            return error;

        convertBytes2Data(sizeof(bytes), bytes, data);

        for (i = 0; i < 128; i++) {
                record.pFIR[i] = (double)data[i] / (1 << 22);
        }

        record.Shift_FIR = data[i++];   ///< Exponent of HX data
        record.leakageFactor = (float)data[i++] / (1 << (23));  ///< Excursion model integration leakage
        record.ReCorrection = (float)data[i++] / (1 << (23));   ///< Correction factor for Re
        record.xInitMargin = (float)data[i++] / (1 << (23 - 2));        ///< (can change) Margin on excursion model during startup
        record.xDamageMargin = (float)data[i++] / (1 << (23 - 2));      ///< Margin on excursion modelwhen damage has been detected
        record.xMargin = (float)data[i++] / (1 << (23 - 2));    ///< Margin on excursion model activated when LookaHead is 0
        record.Bl = (float)data[i++] / (1 << (23 - 2)); ///< Loudspeaker force factor
        record.fRes = data[i++];        ///< (can change) Estimated Speaker Resonance Compensation Filter cutoff frequency
        record.fResInit = data[i++];    ///< Initial Speaker Resonance Compensation Filter cutoff frequency
        record.Qt = (float)data[i++] / (1 << (23 - 6)); ///< Speaker Resonance Compensation Filter Q-factor
        record.xMax = (float)data[i++] / (1 << (23 - 7));       ///< Maximum excursion of the speaker membrane
        record.tMax = (float)data[i++] / (1 << (23 - 9));       ///< Maximum Temperature of the speaker coil
        record.tCoefA = (float)data[i++] / (1 << 23);   ///< (can change) Temperature coefficient

        if (xmodel) {
                //xmodel 0xc1
                for (i = 0; i < 128; i++)
                {
                   fft[127 - i] =(double)data[i] / (1 << 22);
                }
                realfft_split(fft, 128);
                untangle_leakage(fft, untangled, 128, -record.leakageFactor);
                for (i = 0; i < 128 / 2; i++)
                        waveform[i] = 2 * untangled[i];
        } else {
                //zmodel 0x86
                for (i = 0; i < 128; i++)
                        fft[i] = (double)data[i] / (1 << 22);
                realfft_split(fft, 128);
                untangle(fft, untangled, 128);
                for (i = 0; i < 128 / 2; i++)
                        waveform[i] = 1 / untangled[i];
        }

        if(maxdev > 1)
            tfaContGetSlave(idx, &tfa98xxI2cSlave);
        PRINT("\nDevice[%d]: %s at 0x%02x:\n", idx, tfaContDeviceName(idx), tfa98xxI2cSlave);
        PRINT(  "leakageFactor,"
                "ReCorrection,"
                "xInitMargin,"
                "xDamageMargin,"
                "xMargin,"
                "Bl," "fRes," "fResInit," "Qt," "xMax," "tMax," "tCoefA\n");

//        PRINT_FILE(csv, "%f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%.2f,%.2f,%.2f,%.4f\n",
        PRINT(
                "%f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%.2f,%.2f,%.2f,%.4f\n",
                //                record.Shift_FIR,        ///< Exponent of HX data: always 0
                record.leakageFactor,   ///< Excursion model integration leakage
                record.ReCorrection,    ///< Correction factor for Re
                record.xInitMargin,     ///< * Margin on excursion model during startup
                record.xDamageMargin,   ///< Margin on excursion modelwhen damage has been detected
                record.xMargin, ///< Margin on excursion model activated when LookaHead is 0
                record.Bl,      ///< Loudspeaker force factor
                record.fRes,    ///< * Estimated Speaker Resonance Compensation Filter cutoff frequency
                record.fResInit,        ///< Initial Speaker Resonance Compensation Filter cutoff frequency
                record.Qt,      ///< Speaker Resonance Compensation Filter Q-factor
                record.xMax,    ///< Maximum excursion of the speaker membrane
                record.tMax,    ///< Maximum Temperature of the speaker coil
                record.tCoefA   ///< * beetje Temperature coefficient
            );

//        PRINT_FILE(csv,"%s_rawdata, pFIR,fftin, fft, untangled, waveform\n", xmodel?"x":"z");
//        for(i=0;i<128;i++){
//            PRINT_FILE(csv, "0x%03x,%f,%f,%f,%f,%f\n", data[i], record.pFIR[i], fftin[i],  fft[i], untangled[i], waveform[i]);
//            //                PRINT_FILE(csv, "%f, %f,n", record.pFIR[i], waveform[i], (unsigned int)data[127-i] );
//        }

        PRINT_FILE(outfile, "Hz, %s\n", xmodel ? "xcursion mm/V" : "impedance Ohm");
        hz = xmodel ? (int)(62.5) : 40;
        i = xmodel ? 1 : 0;

        for (; i < 64; i++) {
            PRINT_FILE(outfile, "%d,%f\n", hz, waveform[i]);
            hz += (int)(62.5);
        }

        if (tfa98xxLiveData_verbose)
                PRINT("FIR: 0x%03x\n", data[0]);

        if (csv && csv[0] != '\0' && strcmp(csv,"stdout"))
            fclose(outfile);

        return error;
}

int nxpTfa98xxSetVIsCalToConfig( Tfa98xx_handle_t *handlesIn,
                                 float VIsCal,
                                 int idx )
{
    int iVIsCal = 0;
   Tfa98xx_Config_t pParameters = {0};
   Tfa98xx_Error_t err;

    iVIsCal =(int)(VIsCal*(1<<(23-7)));

   err = Tfa98xx_DspReadConfig(handlesIn[idx], TFA98XX_CONFIG_LENGTH, pParameters);

    pParameters[0] = (iVIsCal>>16)&0xFF;
    pParameters[1] = (iVIsCal>>8)&0xFF;
    pParameters[2] = (iVIsCal)&0xFF;

   err = Tfa98xx_DspWriteConfig(handlesIn[idx], TFA98XX_CONFIG_LENGTH, pParameters);

   return err;
}

int nxpTfa98xxSetVsenseCalToConfig( Tfa98xx_handle_t *handlesIn,
                                    float Vsense,
                                    int idx )
{
    int iVsense = 0;
   Tfa98xx_Config_t pParameters = {0};
   Tfa98xx_Error_t err;

    iVsense =(int)(Vsense*(1<<(23-10)));

   err = Tfa98xx_DspReadConfig(handlesIn[idx], TFA98XX_CONFIG_LENGTH, pParameters);

    pParameters[3] = (iVsense>>16)&0xFF;
    pParameters[4] = (iVsense>>8)&0xFF;
    pParameters[5] = (iVsense)&0xFF;

   err = Tfa98xx_DspWriteConfig(handlesIn[idx], TFA98XX_CONFIG_LENGTH, pParameters);

   return err;
}

int nxpTfa98xxSetIsenseCalToConfig( Tfa98xx_handle_t *handlesIn,
                                    float Isense,
                                    int idx )
{
    int iIsense = 0;
   Tfa98xx_Config_t pParameters = {0};
   Tfa98xx_Error_t err;

    iIsense =(int)(Isense*(1<<(23-10)));

   err = Tfa98xx_DspReadConfig(handlesIn[idx], TFA98XX_CONFIG_LENGTH, pParameters);

    pParameters[6] = (iIsense>>16)&0xFF;
    pParameters[7] = (iIsense>>8)&0xFF;
    pParameters[8] = (iIsense)&0xFF;

   err = Tfa98xx_DspWriteConfig(handlesIn[idx], TFA98XX_CONFIG_LENGTH, pParameters);

   return err;
}

int nxpTfa98xxGetVICalfromConfig(   Tfa98xx_handle_t *handlesIn,
                                    float *VIsCal,
                                    float *Vsense,
                                    float *Isense,
                                    int idx )
{
   unsigned char bytes[3 * 55];
   int data[55];
   Tfa98xx_Error_t err;

   err = Tfa98xx_DspReadConfig(handlesIn[idx], TFA98XX_CONFIG_LENGTH, bytes);

   convertBytes2Data(sizeof(bytes), bytes, data);

    *VIsCal = (float)data[0] / (1 << (23 - 7));
   *Vsense = (float)data[1] / (1 << (23 - 10));
   *Isense = (float)data[2] / (1 << (23 - 10));

   return err;
}

int nxpTfa98xxSetAgcGainMaxDBToPreset( Tfa98xx_handle_t *handlesIn,
                                       float agcGainMaxDB,
                                       int idx )
{
    int gainMax = 0;
   unsigned char bytes[252] = {0};
   Tfa98xx_Preset_t pParameters = {0};
   Tfa98xx_Error_t err;
   int i = 0;

    gainMax =(int)(agcGainMaxDB*(1<<(23-8)));

   err = Tfa98xx_DspReadPreset(handlesIn[idx], (TFA98XX_CONFIG_LENGTH+TFA98XX_PRESET_LENGTH), bytes);

   for (i=0; i<87; i++)
   {
      pParameters[i] = bytes[i+165];
   }
    pParameters[39] = (gainMax>>16)&0xFF;
    pParameters[40] = (gainMax>>8)&0xFF;
    pParameters[41] = (gainMax)&0xFF;

   err = Tfa98xx_DspWritePreset(handlesIn[idx], TFA98XX_PRESET_LENGTH, pParameters);

   return err;
}

int nxpTfa98xxGetAgcGainMaxDBfromPreset(  Tfa98xx_handle_t *handlesIn,
                                          float *agcGainMaxDB,
                                          int idx )
{
   unsigned char bytes[252];
   int data[84];
   Tfa98xx_Error_t err;

   err = Tfa98xx_DspReadPreset(handlesIn[idx], (TFA98XX_CONFIG_LENGTH+TFA98XX_PRESET_LENGTH), bytes);

   convertBytes2Data(sizeof(bytes), bytes, data);

    *agcGainMaxDB = (float)data[68] / (1 << (23 - 8));

   return err;
}

