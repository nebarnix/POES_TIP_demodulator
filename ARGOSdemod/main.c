#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include <complex.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <tgmath.h>
#include <time.h>
#include <conio.h>

#include "config.h" //float vs double and terminal type set here

//#include "../common/metadata.h"
#include "../common/wave.h"
#include "../common/AGC.h"
#include "../common/CarrierTrackPLL.h"
#include "../common/LowPassFilter.h"
#include "../common/MMClockRecovery.h"
#include "../common/GardenerClockRecovery.h"
#include "../common/ManchesterDecode.h"
#include "ByteSync.h"

#define TRUE 1
#define FALSE 0

#define DEFAULT_CHUNKSIZE  (2400)
//small chunk sizes (<2000) lead to weird time axis problems AND (separately) buffer overflows!
//TODO: Figure out what the heck small chunk sizes mess up
//BUT we want chunk sizes under the minorframe time for meta tracking.
//There are 10 minor frames per second means chunks should be under Sample rate/10 or 4800

#define DSP_MAX_CARRIER_DEVIATION   (550.0) //was (550.0)

//new values have units of radians per second
 //#define DSP_PLL_LOCK_THRESH         (0.1)      //was (1.00) //AR0.1) (doesn't really do anything if the track and acquire gains are the same)
 #define DSP_PLL_LOCK_ALPHA          (3.1831)   //(0.004) at 5khz
 #define DSP_PLL_ACQ_GAIN            (16)     //0.0015 //was (0.015) at 5khz (16 decoded the best at 5khz)
 #define DSP_PLL_TRCK_GAIN           DSP_PLL_ACQ_GAIN       // 0.0015 //was (0.015) at 5khz
// #define DSP_SQLCH_THRESH            (0.15)     //was (0.25) //was (0.15) //AR0.3 at 5khz


//old values, dependant on sample rate
 #define DSP_PLL_LOCK_THRESH         (0.1) //was (1.00) //AR0.1) (doesn't really do anything if the track and acquire gains are the same)
// #define DSP_PLL_LOCK_ALPHA          (0.004)
// #define DSP_PLL_ACQ_GAIN            (0.015) //0.0015 //was (0.015)
// #define DSP_PLL_TRCK_GAIN           (0.015) // 0.0015 //was (0.015)
 #define DSP_SQLCH_THRESH            (0.15) //was (0.25) //was (0.15) //AR0.3

//new values have units of radians per second
#define DSP_AGC_ATCK_RATE           (79.5775) //(1e-1)//(0.5e-1) //was (1e-1) //attack is when gain is INCREASING (weak signal)
#define DSP_AGC_DCY_RATE            (159.1549) //(2e-1) //was (1e-1) //decay is when the gain is REDUCING (strong signal)
//old values, dependant on sample rate
//#define DSP_AGC_ATCK_RATE           (1e-1)//(0.5e-1) //was (1e-1) //attack is when gain is INCREASING (weak signal)
//#define DSP_AGC_DCY_RATE            (2e-1) //was (1e-1) //decay is when the gain is REDUCING (strong signal)

//#define DSP_AGCC_GAIN               (0.001) //(0.0005) //was (0.001) //AR0.0015
#define DSP_AGCC_GAIN               (0.7958) //(0.001) //(0.0005) //was (0.001) //AR0.0015
#define DSP_LPF_FC                  (700) //(was (700)
#define DSP_LPF_ORDER               (50) //was (50)

#define DSP_GDNR_ERR_LIM            (0.1) //was 0.1
#define DSP_GDNR_GAIN               (3.0) //was 2.5 //was 3.0
#define DSP_BAUD                    (400*2.0)
#define DSP_MCHSTR_RESYNC_LVL       (0.5) //was (0.5)

unsigned int CheckSum(unsigned char *dataStreamReal, unsigned long nSamples)
   {
   unsigned int sum=0;
   unsigned long idx;
   for(idx = 0; idx < nSamples; idx++)
      {
      sum += (dataStreamReal[idx]);
      //printf("%.2X %ld,",dataStreamReal[idx]);
      }
   //printf("\n");
   return sum;
   }

const char *get_filename_ext(const char *filename)
   {
   const char *dot = strrchr(filename, '.');
   if(!dot || dot == filename)
      return "";
   return dot + 1;
   }

int main(int argc, char **argv)
   {
   //Wave variable
   HEADER header;

   //Files we will use
   FILE *inFilePtr=NULL;
   FILE *minorFrameFile=NULL;
   FILE *rawOutFilePtr=NULL;

   unsigned long chunkSize = DEFAULT_CHUNKSIZE, nSamples, i=0, idx, nSymbols, nBits, totalSymbols=0, totalBits=0, totalSamples=0;

   DECIMAL_TYPE *dataStreamReal=NULL, *dataStreamSymbols=NULL, *lockSignalStream=NULL;
   DECIMAL_TYPE Fs;
   //DECIMAL_TYPE LPF_Fc;
   DECIMAL_TYPE averagePhase, percentComplete=0;
   DECIMAL_TYPE normFactor=0;

   DECIMAL_TYPE *filterCoeffs=NULL, *waveDataTime=NULL;
   DECIMAL_TYPE complex *waveData=NULL;


   //unsigned int CheckSum1=0, CheckSum2=0, CheckSum3=0;
   int nFrames=0, totalFrames=0,c;

   unsigned char *dataStreamBits=NULL;
   char *inFileName=NULL;
   char outFileName[100];
   char outputRawFiles=0;

   //const char *build_date = __DATE__;
   printf("Project Desert Tortoise: ARGOS Demodulator by Nebarnix.\nBuild date: %s\n",__DATE__);

   while ((c = getopt (argc, argv, "rn:c:")) != -1)
      {
      switch (c)
         {
         case 'r':
            outputRawFiles = 1;
            printf("Outputting Debugging Raw Files\n");
            break;
         case 'n':
            if(optarg == NULL)
               {
               printf("Static gain unspecified");
               return 1;
               }
            normFactor = atof(optarg);
            printf("Static Gain Override %f\n",normFactor);
            break;
         case 'c':
            if(optarg == NULL)
               {
               printf("Chucksize unspecified");
               return 1;
               }
            chunkSize = atoi(optarg);
            if(chunkSize != DEFAULT_CHUNKSIZE)
               printf("Override: Using %ld chunkSize\n",chunkSize);
            break;
         case '?':
            /*if (optopt == 'c' || optopt == 'n')
               fprintf (stderr, "Option -%c requires an argument.\n", optopt);
            else if (isprint (optopt))
               fprintf (stderr, "Unknown option `-%c'.\n", optopt);
            else
               fprintf (stderr,
               "Unknown option character `\\x%x'.\n",
               optopt);*/
               fprintf (stderr, "-n <int> : set initial audio gain\n");
               fprintf (stderr, "-c <int> : set chunksize\n");
               return 1;
         default:
            fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
            abort ();
         }
      }
   if(chunkSize == DEFAULT_CHUNKSIZE)
      printf("Using default %ld chunkSize\n",chunkSize);

   //Allocate the memory we will need
   filterCoeffs = malloc(sizeof(DECIMAL_TYPE) * DSP_LPF_ORDER);
   inFileName = (char*) malloc(sizeof(char) * 1024);
   waveData = (DECIMAL_TYPE complex*) malloc(sizeof(DECIMAL_TYPE complex) * chunkSize);
   waveDataTime   = (DECIMAL_TYPE *) malloc(sizeof(DECIMAL_TYPE ) * chunkSize);
   dataStreamReal = (DECIMAL_TYPE *) malloc(sizeof(DECIMAL_TYPE ) * chunkSize);
   lockSignalStream = (DECIMAL_TYPE *) malloc(sizeof(DECIMAL_TYPE) * chunkSize);
   dataStreamSymbols = (DECIMAL_TYPE *) malloc(sizeof(DECIMAL_TYPE) * chunkSize);
   dataStreamBits = (unsigned char*) malloc(sizeof(unsigned char) * chunkSize);

   if (dataStreamBits == NULL ||
      filterCoeffs == NULL ||
      inFileName == NULL ||
      waveDataTime == NULL ||
      waveData  == NULL ||
      dataStreamReal == NULL ||
      lockSignalStream == NULL ||
      dataStreamSymbols  == NULL)
      {
      printf("Error in malloc\n");
      exit(1);
      }

   // get file path
   char cwd[1024];
   if (getcwd(cwd, sizeof(cwd)) != NULL)
      {

      // get inFileName from command line
      if (argc < 2)
         {
         printf("No wave file specified\n");
         return 1;
         }
      strcpy(inFileName, argv[optind]);
      printf("%s\n", inFileName);
      }

   // open files
   printf("Opening IO files..\n");
   inFilePtr = fopen(inFileName, "rb");

   time_t t = time(NULL);
   struct tm tm = *localtime(&t);
   //printf("now: %d-%d-%d %d:%d:%d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
   snprintf(outFileName, 100,"packets_%4d%02d%02d_%02d%02d%02d.txt",tm.tm_year + 1900,tm.tm_mon + 1,tm.tm_mday,tm.tm_hour, tm.tm_min, tm.tm_sec);
   minorFrameFile = fopen(outFileName, "w");

   if (minorFrameFile == NULL ||
      inFilePtr == NULL)
      {
      printf("Error opening output files\n");
      exit(1);
      }

   if(outputRawFiles == 1)
      {
      rawOutFilePtr = fopen("output.raw", "wb");
      if (rawOutFilePtr == NULL)
         {
         printf("Error opening output file\n");
         exit(1);
         }
      }

   if(strcasecmp(get_filename_ext(inFileName),"wav") == 0)
      header = ReadWavHeader(inFilePtr);
   else
      {
      printf("RAW files not yet supported :(\n");
      exit(1);
      }
   //else if(strcasecmp(get_filename_ext(inFileName),"raw") == 0)
     // header = ReadRawHeader(inFilePtr);

   Fs = (DECIMAL_TYPE)header.sample_rate;
   long num_samples = (8 * header.data_size) / (header.channels * header.bits_per_sample);

   printf("Sample Rate %.2fKHz and %d bits per sample. Total samples %ld\n", Fs/1000.0, header.bits_per_sample ,num_samples);

   printHeaderInfo(header);

   MakeLPFIR(filterCoeffs, DSP_LPF_ORDER, DSP_LPF_FC, Fs, 1);

   while(!feof(inFilePtr))
      {
      nSamples = GetComplexWaveChunk(inFilePtr, header, waveData, waveDataTime, chunkSize);

      if(outputRawFiles == 1) fwrite(waveData, sizeof(double), nSamples, rawOutFilePtr);
      //if(outputRawFiles == 1) fwrite(waveDataTime, sizeof(double), nSamples, rawOutFilePtr);

      if(i == 0 && normFactor == 0)
         {
         normFactor = StaticGain(waveData, nSamples, 1.0);
         //normFactor = 1;
         printf("Normalization Factor: %f\n",normFactor);
         }

      i+=nSamples;

      //NormalizingAGCC(waveData, nSamples, normFactor, DSP_AGCC_GAIN*(2.0*M_PI/Fs));//this is no longer neccesary because the PLL is now normalizing

      averagePhase = CarrierTrackPLL(waveData, dataStreamReal, lockSignalStream, nSamples, Fs, DSP_MAX_CARRIER_DEVIATION, DSP_PLL_LOCK_THRESH, DSP_PLL_LOCK_ALPHA*(2.0*M_PI/Fs), DSP_PLL_ACQ_GAIN*(2.0*M_PI/Fs), DSP_PLL_TRCK_GAIN*(2.0*M_PI/Fs));
      //make compiler shut up
      (void)averagePhase;
      LowPassFilter(dataStreamReal, nSamples, filterCoeffs, DSP_LPF_ORDER);

      NormalizingAGC(dataStreamReal, nSamples,  normFactor, DSP_AGC_ATCK_RATE*(2.0*M_PI/Fs), DSP_AGC_DCY_RATE*(2.0*M_PI/Fs));


      if(outputRawFiles == 1)
         fwrite(dataStreamReal, sizeof(DECIMAL_TYPE), nSamples,rawOutFilePtr);

      Squelch(dataStreamReal, lockSignalStream, nSamples, DSP_SQLCH_THRESH);
      //nSymbols = MMClockRecovery(dataStreamReal, waveDataTime, nSamples, dataStreamSymbols, Fs, DSP_BAUD, 3, 0.15);
      nSymbols = GardenerClockRecovery(dataStreamReal, waveDataTime, nSamples, dataStreamSymbols, Fs, DSP_BAUD, DSP_GDNR_ERR_LIM, DSP_GDNR_GAIN);

      //fwrite(dataStreamReal, sizeof(DECIMAL_TYPE), nSamples,rawOutFilePtr);

      nBits = ManchesterDecode(dataStreamSymbols, waveDataTime, nSymbols, dataStreamBits, DSP_MCHSTR_RESYNC_LVL);
      // fwrite(dataStreamBits, sizeof(char), nBits,rawOutFilePtr);
      nFrames = FindSyncWords(dataStreamBits, waveDataTime, nBits, "0001011110000", 13, minorFrameFile);

      totalBits += nBits;
      totalFrames += nFrames;
      totalSymbols += nSymbols;
      totalSamples += nSamples;
      if((((DECIMAL_TYPE)( i) / num_samples)*100.0 - percentComplete > 0.15) || feof(inFilePtr))
         {
         percentComplete = ((DECIMAL_TYPE)( i) / num_samples)*100.0;
         printf("\r");
         //printf("\n");
         printf("%0.1f%% %0.3f Ks : %0.1f Sec: %ld Sym : %ld Bits : %d Packets", ((DECIMAL_TYPE)( i) / num_samples)*100.0,(totalSamples)/1000.0, waveDataTime[0], totalSymbols, totalBits, totalFrames);
         }


      /*for(idx=0; idx < nSamples; idx++)
         {
         fVal = (crealf(waveData[i]));
         fwrite(&fVal,sizeof(fVal),1,rawOutFilePtr);
         fVal = (cimagf(waveData[i]));
         fwrite(&fVal,sizeof(fVal),1,rawOutFilePtr);
         }*/
      }

   //printf("\nChecksum1=%X Checksum2=%X Checksum3=%X", CheckSum1,CheckSum2,CheckSum3);
   //printf("\nAll done! Closing files and exiting.\nENJOY YOUR BITS AND HAVE A NICE DAY\n");

   if(outputRawFiles == 1)
      fclose(rawOutFilePtr);

   if (fclose(inFilePtr)) { printf("error closing file."); exit(-1); }
   if (fclose(minorFrameFile)) { printf("error closing file."); exit(-1); }

   if(totalFrames == 0)
   {
      printf("\n\nNone bits found :(\nRemoving output file and exiting.\nMAY YOU HAVE MORE BETTER BITS ANOTHER DAY\n");
      remove(outFileName); //NO MORE ZERO SIZED FILE LITTER
   }
   else
      printf("\nAll done! Closing files and exiting.\nENJOY YOUR BITS AND HAVE A NICE DAY\n");

   // cleanup before quitting
   free(inFileName);
   free(dataStreamSymbols);
   free(filterCoeffs);
   free(dataStreamReal);
   free(waveData);
   free(dataStreamBits);
   free(waveDataTime);
   free(lockSignalStream);

   //quit
   //fflush(stdout);
   return 0;
   }
