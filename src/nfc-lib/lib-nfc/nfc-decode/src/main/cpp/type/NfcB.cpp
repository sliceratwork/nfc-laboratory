/*

  Copyright (c) 2021 Jose Vicente Campos Martinez - <josevcm@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

#include "NfcB.h"

#ifdef DEBUG_SIGNAL
#define DEBUG_ASK_EDGE_CHANNEL 1
#define DEBUG_ASK_SYNC_CHANNEL 2
#endif

#define SOF_BEGIN 0
#define SOF_IDLE 1
#define SOF_END 2

namespace nfc {

enum PatternType
{
   Invalid = 0,
   NoPattern = 1,
   PatternL = 2,
   PatternH = 3
};

struct NfcB::Impl
{
   rt::Logger log {"NfcB"};

   DecoderStatus *decoder;

   // bitrate parameters
   BitrateParams bitrateParams[4] {0,};

   // detected symbol status
   SymbolStatus symbolStatus {0,};

   // bit stream status
   StreamStatus streamStatus {0,};

   // frame processing status
   FrameStatus frameStatus {0,};

   // protocol processing status
   ProtocolStatus protocolStatus {0,};

   // modulation status for each bitrate
   ModulationStatus modulationStatus[4] {0,};

   // minimum modulation threshold to detect valid signal for NFC-B (default 10%)
   float minimumModulationThreshold = 0.10f;

   // minimum modulation threshold to detect valid signal for NFC-B (default 25%)
   float maximumModulationThreshold = 0.50f;

   // last detected frame end
   unsigned int lastFrameEnd = 0;

   // chained frame flags
   unsigned int chainedFlags = 0;

   explicit Impl(DecoderStatus *decoder) : decoder(decoder)
   {
   }

   /*
    * Configure NFC-B modulation
    */
   inline void configure(long sampleRate)
   {
      log.info("--------------------------------------------");
      log.info("initializing NFC-B decoder");
      log.info("--------------------------------------------");
      log.info("\tsignalSampleRate     {}", {decoder->sampleRate});
      log.info("\tpowerLevelThreshold  {}", {decoder->powerLevelThreshold});
      log.info("\tmodulationThreshold  {} -> {}", {minimumModulationThreshold, maximumModulationThreshold});

      // clear detected symbol status
      symbolStatus = {0,};

      // clear bit stream status
      streamStatus = {0,};

      // clear frame processing status
      frameStatus = {0,};

      // clear last detected frame end
      lastFrameEnd = 0;

      // clear chained flags
      chainedFlags = 0;

      // compute symbol parameters for 106Kbps, 212Kbps, 424Kbps and 848Kbps
      for (int rate = r106k; rate <= r424k; rate++)
      {
         // clear bitrate parameters
         bitrateParams[rate] = {0,};

         // clear modulation parameters
         modulationStatus[rate] = {0,};

         // configure bitrate parametes
         BitrateParams *bitrate = bitrateParams + rate;

         // set tech type and rate
         bitrate->techType = TechType::NfcB;
         bitrate->rateType = rate;

         // symbol timing parameters
         bitrate->symbolsPerSecond = BaseFrequency / (128 >> rate);

         // number of samples per symbol
         bitrate->period1SymbolSamples = int(round(decoder->signalParams.sampleTimeUnit * (128 >> rate))); // full symbol samples
         bitrate->period2SymbolSamples = int(round(decoder->signalParams.sampleTimeUnit * (64 >> rate))); // half symbol samples
         bitrate->period4SymbolSamples = int(round(decoder->signalParams.sampleTimeUnit * (32 >> rate))); // quarter of symbol...
         bitrate->period8SymbolSamples = int(round(decoder->signalParams.sampleTimeUnit * (16 >> rate))); // and so on...

         // delay guard for each symbol rate
         bitrate->symbolDelayDetect = rate > r106k ? bitrateParams[rate - 1].symbolDelayDetect + bitrateParams[rate - 1].period1SymbolSamples : 0;

         // moving average offsets
         bitrate->offsetSignalIndex = SignalBufferLength - bitrate->symbolDelayDetect;
         bitrate->offsetSymbolIndex = SignalBufferLength - bitrate->symbolDelayDetect - bitrate->period1SymbolSamples;
         bitrate->offsetFilterIndex = SignalBufferLength - bitrate->symbolDelayDetect - bitrate->period4SymbolSamples;
         bitrate->offsetDetectIndex = SignalBufferLength - bitrate->symbolDelayDetect - bitrate->period8SymbolSamples;

         // exponential symbol average
         bitrate->symbolAverageW0 = float(1 - 5.0 / bitrate->period1SymbolSamples);
         bitrate->symbolAverageW1 = float(1 - bitrate->symbolAverageW0);

         log.info("{} kpbs parameters:", {round(bitrate->symbolsPerSecond / 1E3)});
         log.info("\tsymbolsPerSecond     {}", {bitrate->symbolsPerSecond});
         log.info("\tperiod1SymbolSamples {} ({} us)", {bitrate->period1SymbolSamples, 1E6 * bitrate->period1SymbolSamples / decoder->sampleRate});
         log.info("\tperiod2SymbolSamples {} ({} us)", {bitrate->period2SymbolSamples, 1E6 * bitrate->period2SymbolSamples / decoder->sampleRate});
         log.info("\tperiod4SymbolSamples {} ({} us)", {bitrate->period4SymbolSamples, 1E6 * bitrate->period4SymbolSamples / decoder->sampleRate});
         log.info("\tperiod8SymbolSamples {} ({} us)", {bitrate->period8SymbolSamples, 1E6 * bitrate->period8SymbolSamples / decoder->sampleRate});
         log.info("\tsymbolDelayDetect    {} ({} us)", {bitrate->symbolDelayDetect, 1E6 * bitrate->symbolDelayDetect / decoder->sampleRate});
         log.info("\toffsetSignalIndex    {}", {bitrate->offsetSignalIndex});
         log.info("\toffsetSymbolIndex    {}", {bitrate->offsetSymbolIndex});
         log.info("\toffsetFilterIndex    {}", {bitrate->offsetFilterIndex});
         log.info("\toffsetDetectIndex    {}", {bitrate->offsetDetectIndex});
      }

      // initialize default protocol parameters for start decoding
      protocolStatus.maxFrameSize = 256;
      protocolStatus.startUpGuardTime = int(decoder->signalParams.sampleTimeUnit * 256 * 16 * (1 << 0));
      protocolStatus.frameWaitingTime = int(decoder->signalParams.sampleTimeUnit * 256 * 16 * (1 << 4));
      protocolStatus.frameGuardTime = int(decoder->signalParams.sampleTimeUnit * 128 * 7);
      protocolStatus.requestGuardTime = int(decoder->signalParams.sampleTimeUnit * 7000);

      // initialize frame parameters to default protocol parameters
      frameStatus.startUpGuardTime = protocolStatus.startUpGuardTime;
      frameStatus.frameWaitingTime = protocolStatus.frameWaitingTime;
      frameStatus.frameGuardTime = protocolStatus.frameGuardTime;
      frameStatus.requestGuardTime = protocolStatus.requestGuardTime;

      // initialize exponential average factors for power value
      decoder->signalParams.powerAverageW0 = float(1 - 1E3 / decoder->sampleRate);
      decoder->signalParams.powerAverageW1 = float(1 - decoder->signalParams.powerAverageW0);

      // initialize exponential average factors for signal average
      decoder->signalParams.signalAverageW0 = float(1 - 1E5 / decoder->sampleRate);
      decoder->signalParams.signalAverageW1 = float(1 - decoder->signalParams.signalAverageW0);

      // initialize exponential average factors for signal variance
      decoder->signalParams.signalVarianceW0 = float(1 - 1E5 / decoder->sampleRate);
      decoder->signalParams.signalVarianceW1 = float(1 - decoder->signalParams.signalVarianceW0);

      log.info("Startup parameters");
      log.info("\tmaxFrameSize {} bytes", {protocolStatus.maxFrameSize});
      log.info("\tframeGuardTime {} samples ({} us)", {protocolStatus.frameGuardTime, 1000000.0 * protocolStatus.frameGuardTime / decoder->sampleRate});
      log.info("\tframeWaitingTime {} samples ({} us)", {protocolStatus.frameWaitingTime, 1000000.0 * protocolStatus.frameWaitingTime / decoder->sampleRate});
      log.info("\trequestGuardTime {} samples ({} us)", {protocolStatus.requestGuardTime, 1000000.0 * protocolStatus.requestGuardTime / decoder->sampleRate});
   }

   /*
    * Detect NFC-B modulation
    */
   inline bool detectModulation()
   {
      // ignore low power signals
      if (decoder->signalStatus.powerAverage > decoder->powerLevelThreshold)
      {
         // POLL frame ASK detector for  106Kbps, 212Kbps and 424Kbps
         for (int rate = r106k; rate <= r106k; rate++)
         {
            BitrateParams *bitrate = bitrateParams + rate;
            ModulationStatus *modulation = modulationStatus + rate;

            // compute signal pointers for edge detector, current index, slow average, and fast average
            modulation->signalIndex = (bitrate->offsetSignalIndex + decoder->signalClock);
            modulation->filterIndex = (bitrate->offsetFilterIndex + decoder->signalClock); // 1/4 symbol delay
            modulation->detectIndex = (bitrate->offsetDetectIndex + decoder->signalClock); // 1/4 symbol delay

            // get signal samples
            float signalData = decoder->signalStatus.signalData[modulation->signalIndex & (SignalBufferLength - 1)];
            float filterData = decoder->signalStatus.signalData[modulation->filterIndex & (SignalBufferLength - 1)];
            float detectData = decoder->signalStatus.signalData[modulation->detectIndex & (SignalBufferLength - 1)];

            // integrate signal data over 1/4 symbol (slow average)
            modulation->filterIntegrate += signalData; // add new value
            modulation->filterIntegrate -= filterData; // remove delayed value

            // integrate signal data over 1/8 symbol (fast average)
            modulation->detectIntegrate += signalData; // add new value
            modulation->detectIntegrate -= detectData; // remove delayed value

            // signal edge detector
            float edgeDetector = (modulation->filterIntegrate / bitrate->period4SymbolSamples) - (modulation->detectIntegrate / bitrate->period8SymbolSamples);

            // signal modulation deep
            float modulationDeep = (decoder->signalStatus.powerAverage - signalData) / decoder->signalStatus.powerAverage;

#ifdef DEBUG_ASK_EDGE_CHANNEL
            decoder->debug->set(DEBUG_ASK_EDGE_CHANNEL, edgeDetector);
#endif

            // reset modulation if exceed limits
            if (modulationDeep > maximumModulationThreshold)
            {
               modulation->searchStage = SOF_BEGIN;
               modulation->searchStartTime = 0;
               modulation->searchEndTime = 0;
               modulation->detectorPeek = 0;

               return false;
            }

            // search for first falling edge
            switch (modulation->searchStage)
            {
               case SOF_BEGIN:

                  // detect edge at maximum peak
                  if (modulation->detectorPeek < edgeDetector && edgeDetector > 0.001 && modulationDeep > minimumModulationThreshold)
                  {
                     modulation->detectorPeek = edgeDetector;
                     modulation->searchPeakTime = decoder->signalClock;
                     modulation->searchEndTime = decoder->signalClock + bitrate->period4SymbolSamples;
                  }

                  // first edge detect finished
                  if (decoder->signalClock == modulation->searchEndTime)
                  {
                     // if no edge found, reset modulation
                     if (modulation->searchPeakTime)
                     {
                        // sets frame start
                        modulation->symbolStartTime = modulation->searchPeakTime - bitrate->period8SymbolSamples;

                        // trigger next stage
                        modulation->searchStage = SOF_IDLE;
                        modulation->searchStartTime = modulation->searchPeakTime + (10 * bitrate->period1SymbolSamples) - bitrate->period2SymbolSamples; // search falling edge up to 11 etu
                        modulation->searchEndTime = modulation->searchPeakTime + (11 * bitrate->period1SymbolSamples) + bitrate->period2SymbolSamples; // search falling edge up to 11 etu
                        modulation->searchPeakTime = 0;
                        modulation->detectorPeek = 0;
                     }
                     else
                     {
                        modulation->searchStartTime = 0;
                        modulation->searchEndTime = 0;
                     }
                  }

                  break;

               case SOF_IDLE:

                  // rising edge must be between 10 and 11 etus
                  if (decoder->signalClock > modulation->searchStartTime && decoder->signalClock <= modulation->searchEndTime)
                  {
                     // detect edge at maximum peak
                     if (edgeDetector < -0.001 && modulation->detectorPeek > edgeDetector)
                     {
                        modulation->detectorPeek = edgeDetector;
                        modulation->searchPeakTime = decoder->signalClock;
                        modulation->searchEndTime = decoder->signalClock + bitrate->period4SymbolSamples;
                     }

                     // first edge detect finished
                     if (decoder->signalClock == modulation->searchEndTime)
                     {
                        // if no edge found, reset modulation
                        if (modulation->searchPeakTime)
                        {
                           modulation->searchStage = SOF_END;
                           modulation->searchStartTime = modulation->searchPeakTime + (2 * bitrate->period1SymbolSamples) - bitrate->period2SymbolSamples; // search falling edge up to 11 etu
                           modulation->searchEndTime = modulation->searchPeakTime + (3 * bitrate->period1SymbolSamples) + bitrate->period2SymbolSamples; // search falling edge up to 11 etu
                           modulation->searchPeakTime = 0;
                           modulation->detectorPeek = 0;
                        }
                        else
                        {
                           modulation->searchStage = SOF_BEGIN;
                           modulation->searchStartTime = 0;
                           modulation->searchEndTime = 0;
                           modulation->searchPeakTime = 0;
                           modulation->detectorPeek = 0;
                           modulation->symbolStartTime = 0;
                           modulation->symbolEndTime = 0;
                        }
                     }
                  }

                     // during SOF there must not be modulation changes
                  else if (fabs(edgeDetector) > 0.001)
                  {
                     modulation->searchStage = SOF_BEGIN;
                     modulation->searchStartTime = 0;
                     modulation->searchEndTime = 0;
                     modulation->searchPeakTime = 0;
                     modulation->detectorPeek = 0;
                     modulation->symbolStartTime = 0;
                     modulation->symbolEndTime = 0;

                     return false;
                  }

                  break;

               case SOF_END:

                  // falling edge must be between 2 and 3 etus
                  if (decoder->signalClock > modulation->searchStartTime && decoder->signalClock <= modulation->searchEndTime)
                  {
                     // detect edge at maximum peak
                     if (edgeDetector > 0.001 && modulation->detectorPeek < edgeDetector && modulationDeep > minimumModulationThreshold)
                     {
                        modulation->detectorPeek = edgeDetector;
                        modulation->searchPeakTime = decoder->signalClock;
                        modulation->searchEndTime = decoder->signalClock + bitrate->period8SymbolSamples;
                     }

                     // last edge search finished
                     if (decoder->signalClock == modulation->searchEndTime)
                     {
                        // no edge found! reset modulation
                        if (modulation->searchPeakTime)
                        {
                           // set SOF symbol parameters
                           modulation->symbolEndTime = modulation->searchPeakTime - bitrate->period8SymbolSamples;
                           modulation->symbolSyncTime = 0;

                           // setup frame info
                           frameStatus.frameType = PollFrame;
                           frameStatus.symbolRate = bitrate->symbolsPerSecond;
                           frameStatus.frameStart = modulation->symbolStartTime - bitrate->symbolDelayDetect;
                           frameStatus.frameEnd = 0;

                           // reset modulation to continue search
                           modulation->searchStage = SOF_BEGIN;
                           modulation->searchStartTime = 0;
                           modulation->searchEndTime = 0;
                           modulation->searchDeepValue = 0;
                           modulation->detectorPeek = 0;

                           // modulation detected
                           decoder->bitrate = bitrate;
                           decoder->modulation = modulation;

                           return true;
                        }
                        else
                        {
                           modulation->searchStage = SOF_BEGIN;
                           modulation->searchStartTime = 0;
                           modulation->searchEndTime = 0;
                           modulation->searchPeakTime = 0;
                           modulation->detectorPeek = 0;
                           modulation->symbolStartTime = 0;
                           modulation->symbolEndTime = 0;
                        }
                     }
                  }
            }
         }
      }

      return false;
   }

   /*
    * Decode next poll or listen frame
    */
   inline void decodeFrame(sdr::SignalBuffer &samples, std::list<NfcFrame> &frames)
   {
      if (frameStatus.frameType == FrameType::PollFrame)
      {
         decodePollFrame(samples, frames);
      }

      if (frameStatus.frameType == FrameType::ListenFrame)
      {
         decodeListenFrame(samples, frames);
      }
   }

   /*
    * Decode next poll frame
    */
   inline bool decodePollFrame(sdr::SignalBuffer &buffer, std::list<NfcFrame> &frames)
   {
      int pattern;
      bool frameEnd = false, truncateError = false, streamError = false;

      // decode remaining request frame
      while ((pattern = decodePollFrameSymbolAsk(buffer)) > PatternType::NoPattern)
      {
         // frame ends if found 10 ETU width Pattern-L (10 consecutive bits at value 0)
         if (streamStatus.bits == 9 && !streamStatus.data && pattern == PatternType::PatternL)
            frameEnd = true;

            // frame ends width stream error if start bit is PatternH or end bit is pattern L
         else if ((streamStatus.bits == 0 && pattern == PatternType::PatternH) || (streamStatus.bits == 9 && pattern == PatternType::PatternL))
            streamError = true;

            // frame ends width truncate error max frame size is reached
         else if (streamStatus.bytes == protocolStatus.maxFrameSize)
            truncateError = true;

         // detect end of frame
         if (frameEnd || streamError || truncateError)
         {
            // a valid frame must contain at least one byte of data
            if (streamStatus.bytes > 0)
            {
               frameStatus.frameEnd = symbolStatus.end;

               NfcFrame response = NfcFrame(TechType::NfcB, FrameType::PollFrame);

               response.setFrameRate(decoder->bitrate->symbolsPerSecond);
               response.setSampleStart(frameStatus.frameStart);
               response.setSampleEnd(frameStatus.frameEnd);
               response.setTimeStart(double(frameStatus.frameStart) / double(decoder->sampleRate));
               response.setTimeEnd(double(frameStatus.frameEnd) / double(decoder->sampleRate));

               if (truncateError || streamError)
                  response.setFrameFlags(FrameFlags::Truncated);

               // add bytes to frame and flip to prepare read
               response.put(streamStatus.buffer, streamStatus.bytes).flip();

               // clear modulation status for next frame search
               decoder->modulation->symbolStartTime = 0;
               decoder->modulation->symbolEndTime = 0;
               decoder->modulation->symbolSyncTime = 0;
               decoder->modulation->filterIntegrate = 0;
               decoder->modulation->detectIntegrate = 0;
               decoder->modulation->phaseIntegrate = 0;

               // clear stream status
               streamStatus = {0,};

               // process frame
               process(response);

               // add to frame list
               frames.push_back(response);

               return true;
            }

            // reset modulation and restart frame detection
            resetModulation();

            // no valid frame found
            return false;
         }

         // decode next bit
         if (streamStatus.bits < 9)
         {
            if (streamStatus.bits > 0)
               streamStatus.data |= (symbolStatus.value << (streamStatus.bits - 1));

            streamStatus.bits++;
         }
            // store full byte in stream buffer
         else
         {
            streamStatus.buffer[streamStatus.bytes++] = streamStatus.data;
            streamStatus.data = 0;
            streamStatus.bits = 0;
         }
      }

      // no frame detected
      return false;
   }

   /*
    * Decode next listen frame
    */
   inline bool decodeListenFrame(sdr::SignalBuffer &buffer, std::list<NfcFrame> &frames)
   {
      decoder->bitrate = nullptr;
      decoder->modulation = nullptr;

      return false;
   }

   /*
    * Decode one ASK modulated poll frame symbol
    */
   inline int decodePollFrameSymbolAsk(sdr::SignalBuffer &buffer)
   {
      symbolStatus.pattern = PatternType::Invalid;

      BitrateParams *bitrate = decoder->bitrate;
      ModulationStatus *modulation = decoder->modulation;

      while (decoder->nextSample(buffer))
      {
         // compute signal pointers for edge detector, current index, slow average, and fast average
         modulation->signalIndex = (bitrate->offsetSignalIndex + decoder->signalClock);
         modulation->filterIndex = (bitrate->offsetFilterIndex + decoder->signalClock);
         modulation->detectIndex = (bitrate->offsetDetectIndex + decoder->signalClock);

         // get signal samples
         float signalData = decoder->signalStatus.signalData[modulation->signalIndex & (SignalBufferLength - 1)]; // current signal value
         float filterData = decoder->signalStatus.signalData[modulation->filterIndex & (SignalBufferLength - 1)]; // 1/4 symbol delay (slow average)
         float detectData = decoder->signalStatus.signalData[modulation->detectIndex & (SignalBufferLength - 1)]; // 1/8 symbol delay (fast average)

         // moving average over 1/4 symbol (slow average)
         modulation->filterIntegrate += signalData; // add new value
         modulation->filterIntegrate -= filterData; // remove delayed value

         // moving average over 1/8 symbol (fast average)
         modulation->detectIntegrate += signalData; // add new value
         modulation->detectIntegrate -= detectData; // remove delayed value

         // subtract fast average from slow average to get signal edge
         float edgeDetector = std::fabs((modulation->filterIntegrate / bitrate->period4SymbolSamples) - (modulation->detectIntegrate / decoder->bitrate->period8SymbolSamples));

         // signal modulation deep
         float modulationDeep = (decoder->signalStatus.powerAverage - signalData) / decoder->signalStatus.powerAverage;

#ifdef DEBUG_ASK_EDGE_CHANNEL
         decoder->debug->set(DEBUG_ASK_EDGE_CHANNEL, edgeDetector);
#endif

#ifdef DEBUG_ASK_SYNC_CHANNEL
         decoder->debug->set(DEBUG_ASK_SYNC_CHANNEL, 0.0f);
#endif

         // edge re-synchronization window
         if (decoder->signalClock > modulation->searchStartTime && decoder->signalClock < modulation->searchEndTime)
         {
            // detect edge at maximum peak
            if (edgeDetector > 0.001 && modulation->detectorPeek < edgeDetector && modulationDeep > minimumModulationThreshold)
            {
               modulation->detectorPeek = edgeDetector;
               modulation->symbolEndTime = decoder->signalClock - bitrate->period8SymbolSamples;
               modulation->symbolSyncTime = 0;
            }
         }

         // estimate next symbol timings
         if (!modulation->symbolSyncTime)
         {
            // estimated symbol start and end
            modulation->symbolStartTime = modulation->symbolEndTime;
            modulation->symbolEndTime = modulation->symbolStartTime + bitrate->period1SymbolSamples;
            modulation->symbolSyncTime = modulation->symbolStartTime + bitrate->period2SymbolSamples;
         }

         // get signal value at symbol synchronization point and finish
         if (decoder->signalClock == modulation->symbolSyncTime)
         {
#ifdef DEBUG_ASK_SYNC_CHANNEL
            decoder->debug->set(DEBUG_ASK_SYNC_CHANNEL, 0.50f);
#endif
            // modulated signal, symbol L -> 0 value
            if (modulationDeep > minimumModulationThreshold)
            {
               // setup symbol info
               symbolStatus.value = 0;
               symbolStatus.start = modulation->symbolStartTime - bitrate->symbolDelayDetect;
               symbolStatus.end = modulation->symbolEndTime - bitrate->symbolDelayDetect;
               symbolStatus.length = symbolStatus.end - symbolStatus.start;
               symbolStatus.pattern = PatternType::PatternL;
            }

               // non modulated signal, symbol H -> 1 value
            else
            {
               // setup symbol info
               symbolStatus.value = 1;
               symbolStatus.start = modulation->symbolStartTime - bitrate->symbolDelayDetect;
               symbolStatus.end = modulation->symbolEndTime - bitrate->symbolDelayDetect;
               symbolStatus.length = symbolStatus.end - symbolStatus.start;
               symbolStatus.pattern = PatternType::PatternH;
            }

            // next edge re-synchronization window
            modulation->searchStartTime = modulation->symbolEndTime - bitrate->period4SymbolSamples;
            modulation->searchEndTime = modulation->symbolEndTime + bitrate->period4SymbolSamples;

            // reset status for next symbol
            modulation->symbolSyncTime = 0;
            modulation->detectorPeek = 0;

            break;
         }
      }

      return symbolStatus.pattern;
   }

   /*
    * Decode one BPSK modulated listen frame symbol
    */
   inline int decodeListenFrameSymbolBpsk(sdr::SignalBuffer &buffer)
   {
      return 0;
   }

   /*
    * Reset modulation status
    */
   inline void resetModulation()
   {
      // reset modulation detection for all rates
      for (int rate = r106k; rate <= r424k; rate++)
      {
         modulationStatus[rate].searchStage = 0;
         modulationStatus[rate].searchStartTime = 0;
         modulationStatus[rate].searchEndTime = 0;
         modulationStatus[rate].searchPulseWidth = 0;
         modulationStatus[rate].searchDeepValue = 0;
         modulationStatus[rate].symbolAverage = 0;
         modulationStatus[rate].symbolPhase = NAN;
         modulationStatus[rate].detectorPeek = 0;
         modulationStatus[rate].correlationPeek = 0;
      }

      // clear stream status
      streamStatus = {0,};

      // clear stream status
      symbolStatus = {0,};

      // clear frame status
      frameStatus.frameType = 0;
      frameStatus.frameStart = 0;
      frameStatus.frameEnd = 0;

      // restore bitrate
      decoder->bitrate = nullptr;

      // restore modulation
      decoder->modulation = nullptr;
   }

   /*
    * Process request or response frame
    */
   inline void process(NfcFrame &frame)
   {
      // for request frame set default response timings, must be overridden by subsequent process functions
      if (frame.isPollFrame())
      {
         frameStatus.frameWaitingTime = protocolStatus.frameWaitingTime;
      }

      while (true)
      {
         if (processREQB(frame))
            break;

         //      if (processHLTA(frame))
         //         break;
         //
         //      if (processSELn(frame))
         //         break;
         //
         //      if (processRATS(frame))
         //         break;
         //
         //      if (processPPSr(frame))
         //         break;
         //
         //      if (processAUTH(frame))
         //         break;
         //
         //      if (processIBlock(frame))
         //         break;
         //
         //      if (processRBlock(frame))
         //         break;
         //
         //      if (processSBlock(frame))
         //         break;

         processOther(frame);

         break;
      }

      // set chained flags
      frame.setFrameFlags(chainedFlags);

      // for request frame set response timings
      if (frame.isPollFrame())
      {
         // update frame timing parameters for receive PICC frame
         if (decoder->bitrate)
         {
            // response guard time TR0min (PICC must not modulate response within this period)
            frameStatus.guardEnd = frameStatus.frameEnd + frameStatus.frameGuardTime + decoder->bitrate->symbolDelayDetect;

            // response delay time WFT (PICC must reply to command before this period)
            frameStatus.waitingEnd = frameStatus.frameEnd + frameStatus.frameWaitingTime + decoder->bitrate->symbolDelayDetect;

            // next frame must be ListenFrame
            frameStatus.frameType = ListenFrame;
         }
      }
      else
      {
         // switch to modulation search
         frameStatus.frameType = 0;

         // reset frame command
         frameStatus.lastCommand = 0;
      }

      // mark last processed frame
      lastFrameEnd = frameStatus.frameEnd;

      // reset frame start
      frameStatus.frameStart = 0;

      // reset frame end
      frameStatus.frameEnd = 0;
   }

   /*
    * Process REQB/WUPB frame
    */
   inline bool processREQB(NfcFrame &frame)
   {
      if (frame.isPollFrame())
      {
         if (frame[0] == CommandType::NFCA_REQB && frame.limit() == 5)
         {
            frameStatus.lastCommand = frame[0];

            // This commands starts or wakeup card communication, so reset the protocol parameters to the default values
            protocolStatus.maxFrameSize = 256;
            protocolStatus.frameGuardTime = int(decoder->signalParams.sampleTimeUnit * 128 * 7);
            protocolStatus.frameWaitingTime = int(decoder->signalParams.sampleTimeUnit * 256 * 16 * (1 << 4));

            // The REQ-A Response must start exactly at 128 * n, n=9, decoder search between n=7 and n=18
            frameStatus.frameGuardTime = decoder->signalParams.sampleTimeUnit * 128 * 7; // REQ-B response guard
            frameStatus.frameWaitingTime = decoder->signalParams.sampleTimeUnit * 128 * 18; // REQ-B response timeout

            // clear chained flags
            chainedFlags = 0;

            // set frame flags
            frame.setFramePhase(FramePhase::SelectionFrame);
            frame.setFrameFlags(!checkCrc(frame) ? FrameFlags::CrcError : 0);

            return true;
         }
      }

      if (frame.isListenFrame())
      {
         if (frameStatus.lastCommand == CommandType::NFCA_REQB)
         {
            frame.setFramePhase(FramePhase::SelectionFrame);

            return true;
         }
      }

      return false;
   }

   /*
    * Process other frames
    */
   inline void processOther(NfcFrame &frame)
   {
      frame.setFramePhase(FramePhase::ApplicationFrame);
      frame.setFrameFlags(!checkCrc(frame) ? FrameFlags::CrcError : 0);
   }

   /*
    * Check NFC-B crc
    */
   inline bool checkCrc(NfcFrame &frame)
   {
      unsigned short crc = 0xFFFF; // NFC-B ISO/IEC 13239
      unsigned short res = 0;

      int length = frame.limit();

      if (length <= 2)
         return false;

      for (int i = 0; i < length - 2; i++)
      {
         auto d = (unsigned char) frame[i];

         d = (d ^ (unsigned int) (crc & 0xff));
         d = (d ^ (d << 4));

         crc = (crc >> 8) ^ ((unsigned short) (d << 8)) ^ ((unsigned short) (d << 3)) ^ ((unsigned short) (d >> 4));
      }

      crc = ~crc;

      res |= ((unsigned int) frame[length - 2] & 0xff);
      res |= ((unsigned int) frame[length - 1] & 0xff) << 8;

      return res == crc;
   }
};

NfcB::NfcB(DecoderStatus *decoder) : self(new Impl(decoder))
{
}

NfcB::~NfcB()
{
   delete self;
}

void NfcB::setModulationThreshold(float min, float max)
{
   self->minimumModulationThreshold = min;
   self->maximumModulationThreshold = max;
}

void NfcB::configure(long sampleRate)
{
   self->configure(sampleRate);
}

bool NfcB::detect()
{
   return self->detectModulation();
}

void NfcB::decode(sdr::SignalBuffer &samples, std::list<NfcFrame> &frames)
{
   self->decodeFrame(samples, frames);
}

}
