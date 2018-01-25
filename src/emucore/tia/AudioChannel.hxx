//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2018 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#ifndef TIA_AUDIO_CHANNEL_HXX
#define TIA_AUDIO_CHANNEL_HXX

#include "bspf.hxx"

class AudioChannel
{
  public:
    AudioChannel();

    void reset();

    void phase0();

    uInt8 phase1();

    void audc(uInt8 value);

    void audf(uInt8 value);

    void audv(uInt8 value);

  private:
    uInt8 myAudc;
    uInt8 myAudv;
    uInt8 myAudf;

    bool myClockEnable;
    bool myNoiseFeedback;
    bool myNoiseCounterBit4;
    bool myPulseCounterHold;

    uInt8 myDivCounter;
    uInt8 myPulseCounter;
    uInt8 myNoiseCounter;

  private:
    AudioChannel(const AudioChannel&);
    AudioChannel(AudioChannel&&);
    AudioChannel& operator=(const AudioChannel&);
    AudioChannel& operator=(AudioChannel&&);
};

#endif // TIA_AUDIO_CHANNEL_HXX