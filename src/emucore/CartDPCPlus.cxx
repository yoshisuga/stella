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
// Copyright (c) 1995-2019 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#ifdef DEBUGGER_SUPPORT
  #include "Debugger.hxx"
#endif
#include "MD5.hxx"
#include "System.hxx"
#include "Thumbulator.hxx"
#include "CartDPCPlus.hxx"
#include "TIA.hxx"
#include "exception/FatalEmulationError.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CartridgeDPCPlus::CartridgeDPCPlus(const ByteBuffer& image, uInt32 size,
                                   const string& md5, const Settings& settings)
  : Cartridge(settings, md5),
    mySize(std::min(size, 32768u)),
    myFastFetch(false),
    myLDAimmediate(false),
    myParameterPointer(0),
    myAudioCycles(0),
    myARMCycles(0),
    myFractionalClocks(0.0),
    myBankOffset(0),
    myFractionalLowMask(0x0F00FF)
{
  // Image is always 32K, but in the case of ROM > 29K, the image is
  // copied to the end of the buffer
  if(mySize < 32768u)
    memset(myImage, 0, 32768);
  memcpy(myImage + (32768u - mySize), image.get(), size);
  createCodeAccessBase(4096 * 6);

  // Pointer to the program ROM (24K @ 3072 byte offset; ignore first 3K)
  myProgramImage = myImage + 0xC00;

  // Pointer to the display RAM
  myDisplayImage = myDPCRAM + 0xC00;

  // Pointer to the Frequency RAM
  myFrequencyImage = myDisplayImage + 0x1000;

  // Create Thumbulator ARM emulator
  bool devSettings = settings.getBool("dev.settings");
  myThumbEmulator = make_unique<Thumbulator>
      (reinterpret_cast<uInt16*>(myImage),
       reinterpret_cast<uInt16*>(myDPCRAM),
       32768,
       devSettings ? settings.getBool("dev.thumb.trapfatal") : false,
       Thumbulator::ConfigureFor::DPCplus,
       this);

  // Currently only one known DPC+ ARM driver exhibits a problem
  // with the default mask to use for DFxFRACLOW
  if(MD5::hash(image, 3*1024) == "8dd73b44fd11c488326ce507cbeb19d1")
    myFractionalLowMask = 0x0F0000;

  setInitialState();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CartridgeDPCPlus::reset()
{
  setInitialState();

  // DPC+ always starts in bank 5
  initializeStartBank(5);

  // Upon reset we switch to the startup bank
  bank(startBank());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CartridgeDPCPlus::setInitialState()
{
  // Reset various ROM and RAM locations
  memset(myDPCRAM, 0, 8192);

  // Copy initial DPC display data and Frequency table state to Harmony RAM
  memcpy(myDisplayImage, myProgramImage + 0x6000, 0x1400);

  // Initialize the DPC data fetcher registers
  for(int i = 0; i < 8; ++i)
  {
    myTops[i] = myBottoms[i] = myFractionalIncrements[i] = 0;
    myFractionalCounters[i] = 0;
    myCounters[i] = 0;
  }

  // Set waveforms to first waveform entry
  myMusicWaveforms[0] = myMusicWaveforms[1] = myMusicWaveforms[2] = 0;

  // Initialize the DPC's random number generator register (must be non-zero)
  myRandomNumber = 0x2B435044; // "DPC+"

  // Initialize various other parameters
  myFastFetch = myLDAimmediate = false;
  myAudioCycles = myARMCycles = 0;
  myFractionalClocks = 0.0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CartridgeDPCPlus::consoleChanged(ConsoleTiming timing)
{
  myThumbEmulator->setConsoleTiming(timing);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CartridgeDPCPlus::install(System& system)
{
  mySystem = &system;

  // Map all of the accesses to call peek and poke
  System::PageAccess access(this, System::PageAccessType::READ);
  for(uInt16 addr = 0x1000; addr < 0x1080; addr += System::PAGE_SIZE)
    mySystem->setPageAccess(addr, access);

  // Install pages for the startup bank
  bank(startBank());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void CartridgeDPCPlus::clockRandomNumberGenerator()
{
  // Update random number generator (32-bit LFSR)
  myRandomNumber = ((myRandomNumber & (1<<10)) ? 0x10adab1e: 0x00) ^
                   ((myRandomNumber >> 11) | (myRandomNumber << 21));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void CartridgeDPCPlus::priorClockRandomNumberGenerator()
{
  // Update random number generator (32-bit LFSR, reversed)
  myRandomNumber = ((myRandomNumber & (1u<<31)) ?
    ((0x10adab1e^myRandomNumber) << 11) | ((0x10adab1e^myRandomNumber) >> 21) :
    (myRandomNumber << 11) | (myRandomNumber >> 21));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void CartridgeDPCPlus::updateMusicModeDataFetchers()
{
  // Calculate the number of cycles since the last update
  uInt32 cycles = uInt32(mySystem->cycles() - myAudioCycles);
  myAudioCycles = mySystem->cycles();

  // Calculate the number of DPC+ OSC clocks since the last update
  double clocks = ((20000.0 * cycles) / 1193191.66666667) + myFractionalClocks;
  uInt32 wholeClocks = uInt32(clocks);
  myFractionalClocks = clocks - double(wholeClocks);

  // Let's update counters and flags of the music mode data fetchers
  if(wholeClocks > 0)
    for(int x = 0; x <= 2; ++x)
      myMusicCounters[x] += myMusicFrequencies[x] * wholeClocks;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void CartridgeDPCPlus::callFunction(uInt8 value)
{
  // myParameter
  uInt16 ROMdata = (myParameter[1] << 8) + myParameter[0];
  switch (value)
  {
    case 0: // Parameter Pointer reset
      myParameterPointer = 0;
      break;
    case 1: // Copy ROM to fetcher
      for(int i = 0; i < myParameter[3]; ++i)
        myDisplayImage[myCounters[myParameter[2] & 0x7]+i] = myProgramImage[ROMdata+i];
      myParameterPointer = 0;
      break;
    case 2: // Copy value to fetcher
      for(int i = 0; i < myParameter[3]; ++i)
        myDisplayImage[myCounters[myParameter[2]]+i] = myParameter[0];
      myParameterPointer = 0;
      break;
      // Call user written ARM code (most likely be C compiled for ARM)
    case 254: // call with IRQ driven audio, no special handling needed at this
              // time for Stella as ARM code "runs in zero 6507 cycles".
    case 255: // call without IRQ driven audio
      try {
        Int32 cycles = Int32(mySystem->cycles() - myARMCycles);
        myARMCycles = mySystem->cycles();

        myThumbEmulator->run(cycles);
      }
      catch(const runtime_error& e) {
        if(!mySystem->autodetectMode())
        {
          FatalEmulationError::raise(e.what());
        }
      }
      break;
    // reserved
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt8 CartridgeDPCPlus::peek(uInt16 address)
{
  address &= 0x0FFF;

  uInt8 peekvalue = myProgramImage[myBankOffset + address];
  uInt8 flag;

  // In debugger/bank-locked mode, we ignore all hotspots and in general
  // anything that can change the internal state of the cart
  if(bankLocked())
    return peekvalue;

  // Check if we're in Fast Fetch mode and the prior byte was an A9 (LDA #value)
  if(myFastFetch && myLDAimmediate)
  {
    if(peekvalue < 0x0028)
      // if #value is a read-register then we want to use that as the address
      address = peekvalue;
  }
  myLDAimmediate = false;

  if(address < 0x0028)
  {
    uInt8 result = 0;

    // Get the index of the data fetcher that's being accessed
    uInt32 index = address & 0x07;
    uInt32 function = (address >> 3) & 0x07;

    // Update flag for selected data fetcher
    flag = (((myTops[index]-(myCounters[index] & 0x00ff)) & 0xFF) > ((myTops[index]-myBottoms[index]) & 0xFF)) ? 0xFF : 0;

    switch(function)
    {
      case 0x00:
      {
        switch(index)
        {
          case 0x00:  // RANDOM0NEXT - advance and return byte 0 of random
            clockRandomNumberGenerator();
            result = myRandomNumber & 0xFF;
            break;

          case 0x01:  // RANDOM0PRIOR - return to prior and return byte 0 of random
            priorClockRandomNumberGenerator();
            result = myRandomNumber & 0xFF;
            break;

          case 0x02:  // RANDOM1
            result = (myRandomNumber>>8) & 0xFF;
            break;

          case 0x03:  // RANDOM2
            result = (myRandomNumber>>16) & 0xFF;
            break;

          case 0x04:  // RANDOM3
            result = (myRandomNumber>>24) & 0xFF;
            break;

          case 0x05: // AMPLITUDE
          {
            // Update the music data fetchers (counter & flag)
            updateMusicModeDataFetchers();

            // using myDisplayImage[] instead of myProgramImage[] because waveforms
            // can be modified during runtime.
            uInt32 i = myDisplayImage[(myMusicWaveforms[0] << 5) + (myMusicCounters[0] >> 27)] +
                       myDisplayImage[(myMusicWaveforms[1] << 5) + (myMusicCounters[1] >> 27)] +
                       myDisplayImage[(myMusicWaveforms[2] << 5) + (myMusicCounters[2] >> 27)];

            result = uInt8(i);
            break;
          }

          case 0x06:  // reserved
          case 0x07:  // reserved
            break;
        }
        break;
      }

      // DFxDATA - display data read
      case 0x01:
      {
        result = myDisplayImage[myCounters[index]];
        myCounters[index] = (myCounters[index] + 0x1) & 0x0fff;
        break;
      }

      // DFxDATAW - display data read AND'd w/flag ("windowed")
      case 0x02:
      {
        result = myDisplayImage[myCounters[index]] & flag;
        myCounters[index] = (myCounters[index] + 0x1) & 0x0fff;
        break;
      }

      // DFxFRACDATA - display data read w/fractional increment
      case 0x03:
      {
        result = myDisplayImage[myFractionalCounters[index] >> 8];
        myFractionalCounters[index] = (myFractionalCounters[index] + myFractionalIncrements[index]) & 0x0fffff;
        break;
      }

      case 0x04:
      {
        switch (index)
        {
          case 0x00:  // DF0FLAG
          case 0x01:  // DF1FLAG
          case 0x02:  // DF2FLAG
          case 0x03:  // DF3FLAG
          {
            result = flag;
            break;
          }
          case 0x04:  // reserved
          case 0x05:  // reserved
          case 0x06:  // reserved
          case 0x07:  // reserved
            break;
        }
        break;
      }

      default:
      {
        result = 0;
      }
    }

    return result;
  }
  else
  {
    // Switch banks if necessary
    switch(address)
    {
      case 0x0FF6:
        // Set the current bank to the first 4k bank
        bank(0);
        break;

      case 0x0FF7:
        // Set the current bank to the second 4k bank
        bank(1);
        break;

      case 0x0FF8:
        // Set the current bank to the third 4k bank
        bank(2);
        break;

      case 0x0FF9:
        // Set the current bank to the fourth 4k bank
        bank(3);
        break;

      case 0x0FFA:
        // Set the current bank to the fifth 4k bank
        bank(4);
        break;

      case 0x0FFB:
        // Set the current bank to the last 4k bank
        bank(5);
        break;

      default:
        break;
    }

    if(myFastFetch)
      myLDAimmediate = (peekvalue == 0xA9);

    return peekvalue;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CartridgeDPCPlus::poke(uInt16 address, uInt8 value)
{
  address &= 0x0FFF;

  if((address >= 0x0028) && (address < 0x0080))
  {
    // Get the index of the data fetcher that's being accessed
    uInt32 index = address & 0x07;
    uInt32 function = ((address - 0x28) >> 3) & 0x0f;

    switch(function)
    {
      // DFxFRACLOW - fractional data pointer low byte
      case 0x00:
        myFractionalCounters[index] =
          (myFractionalCounters[index] & myFractionalLowMask) | (uInt16(value) << 8);
        break;

      // DFxFRACHI - fractional data pointer high byte
      case 0x01:
        myFractionalCounters[index] = ((uInt16(value) & 0x0F) << 16) | (myFractionalCounters[index] & 0x00ffff);
        break;

      //DFxFRACINC - Fractional Increment amount
      case 0x02:
        myFractionalIncrements[index] = value;
        myFractionalCounters[index] = myFractionalCounters[index] & 0x0FFF00;
        break;

      // DFxTOP - set top of window (for reads of DFxDATAW)
      case 0x03:
        myTops[index] = value;
        break;

      // DFxBOT - set bottom of window (for reads of DFxDATAW)
      case 0x04:
        myBottoms[index] = value;
        break;

      // DFxLOW - data pointer low byte
      case 0x05:
        myCounters[index] = (myCounters[index] & 0x0F00) | value ;
        break;

      // Control registers
      case 0x06:
        switch (index)
        {
          case 0x00:  // FASTFETCH - turns on LDA #<DFxDATA mode of value is 0
            myFastFetch = (value == 0);
            break;

          case 0x01:  // PARAMETER - set parameter used by CALLFUNCTION (not all functions use the parameter)
            if(myParameterPointer < 8)
              myParameter[myParameterPointer++] = value;
            break;

          case 0x02:  // CALLFUNCTION
            callFunction(value);
            break;

          case 0x03:  // reserved
          case 0x04:  // reserved
            break;

          case 0x05:  // WAVEFORM0
          case 0x06:  // WAVEFORM1
          case 0x07:  // WAVEFORM2
            myMusicWaveforms[index - 5] =  value & 0x7f;
            break;
        }
        break;

      // DFxPUSH - Push value into data bank
      case 0x07:
      {
        myCounters[index] = (myCounters[index] - 0x1) & 0x0fff;
        myDisplayImage[myCounters[index]] = value;
        break;
      }

      // DFxHI - data pointer high byte
      case 0x08:
      {
        myCounters[index] = ((uInt16(value) & 0x0F) << 8) | (myCounters[index] & 0x00ff);
        break;
      }

      case 0x09:
      {
        switch (index)
        {
          case 0x00:  // RRESET - Random Number Generator Reset
          {
            myRandomNumber = 0x2B435044; // "DPC+"
            break;
          }
          case 0x01:  // RWRITE0 - update byte 0 of random number
          {
            myRandomNumber = (myRandomNumber & 0xFFFFFF00) | value;
            break;
          }
          case 0x02:  // RWRITE1 - update byte 1 of random number
          {
            myRandomNumber = (myRandomNumber & 0xFFFF00FF) | (value<<8);
            break;
          }
          case 0x03:  // RWRITE2 - update byte 2 of random number
          {
            myRandomNumber = (myRandomNumber & 0xFF00FFFF) | (value<<16);
            break;
          }
          case 0x04:  // RWRITE3 - update byte 3 of random number
          {
            myRandomNumber = (myRandomNumber & 0x00FFFFFF) | (value<<24);
            break;
          }
          case 0x05:  // NOTE0
          case 0x06:  // NOTE1
          case 0x07:  // NOTE2
          {
            myMusicFrequencies[index-5] = myFrequencyImage[(value<<2)] +
            (myFrequencyImage[(value<<2)+1]<<8) +
            (myFrequencyImage[(value<<2)+2]<<16) +
            (myFrequencyImage[(value<<2)+3]<<24);
            break;
          }
          default:
            break;
        }
        break;
      }

      // DFxWRITE - write into data bank
      case 0x0a:
      {
        myDisplayImage[myCounters[index]] = value;
        myCounters[index] = (myCounters[index] + 0x1) & 0x0fff;
        break;
      }

      default:
      {
        break;
      }
    }
  }
  else
  {
    // Switch banks if necessary
    switch(address)
    {
      case 0x0FF6:
        // Set the current bank to the first 4k bank
        bank(0);
        break;

      case 0x0FF7:
        // Set the current bank to the second 4k bank
        bank(1);
        break;

      case 0x0FF8:
        // Set the current bank to the third 4k bank
        bank(2);
        break;

      case 0x0FF9:
        // Set the current bank to the fourth 4k bank
        bank(3);
        break;

      case 0x0FFA:
        // Set the current bank to the fifth 4k bank
        bank(4);
        break;

      case 0x0FFB:
        // Set the current bank to the last 4k bank
        bank(5);
        break;

      default:
        break;
    }
  }
  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CartridgeDPCPlus::bank(uInt16 bank)
{
  if(bankLocked()) return false;

  // Remember what bank we're in
  myBankOffset = bank << 12;

  // Setup the page access methods for the current bank
  System::PageAccess access(this, System::PageAccessType::READ);

  // Map Program ROM image into the system
  for(uInt16 addr = 0x1080; addr < 0x2000; addr += System::PAGE_SIZE)
  {
    access.codeAccessBase = &myCodeAccessBase[myBankOffset + (addr & 0x0FFF)];
    mySystem->setPageAccess(addr, access);
  }
  return myBankChanged = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt16 CartridgeDPCPlus::getBank() const
{
  return myBankOffset >> 12;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt16 CartridgeDPCPlus::bankCount() const
{
  return 6;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CartridgeDPCPlus::patch(uInt16 address, uInt8 value)
{
  address &= 0x0FFF;

  // For now, we ignore attempts to patch the DPC address space
  if(address >= 0x0080)
  {
    myProgramImage[myBankOffset + (address & 0x0FFF)] = value;
    return myBankChanged = true;
  }
  else
    return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const uInt8* CartridgeDPCPlus::getImage(uInt32& size) const
{
  size = mySize;
  return myImage + (32768u - mySize);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CartridgeDPCPlus::save(Serializer& out) const
{
  try
  {
    // Indicates which bank is currently active
    out.putShort(myBankOffset);

    // Harmony RAM
    out.putByteArray(myDPCRAM, 8192);

    // The top registers for the data fetchers
    out.putByteArray(myTops, 8);

    // The bottom registers for the data fetchers
    out.putByteArray(myBottoms, 8);

    // The counter registers for the data fetchers
    out.putShortArray(myCounters, 8);

    // The counter registers for the fractional data fetchers
    out.putIntArray(myFractionalCounters, 8);

    // The fractional registers for the data fetchers
    out.putByteArray(myFractionalIncrements, 8);

    // The Fast Fetcher Enabled flag
    out.putBool(myFastFetch);
    out.putBool(myLDAimmediate);

    // Control Byte to update
    out.putByteArray(myParameter, 8);

    // The music counters
    out.putIntArray(myMusicCounters, 3);

    // The music frequencies
    out.putIntArray(myMusicFrequencies, 3);

    // The music waveforms
    out.putShortArray(myMusicWaveforms, 3);

    // The random number generator register
    out.putInt(myRandomNumber);

    // Get system cycles and fractional clocks
    out.putLong(myAudioCycles);
    out.putDouble(myFractionalClocks);

    // Clock info for Thumbulator
    out.putLong(myARMCycles);
  }
  catch(...)
  {
    cerr << "ERROR: CartridgeDPCPlus::save" << endl;
    return false;
  }

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CartridgeDPCPlus::load(Serializer& in)
{
  try
  {
    // Indicates which bank is currently active
    myBankOffset = in.getShort();

    // Harmony RAM
    in.getByteArray(myDPCRAM, 8192);

    // The top registers for the data fetchers
    in.getByteArray(myTops, 8);

    // The bottom registers for the data fetchers
    in.getByteArray(myBottoms, 8);

    // The counter registers for the data fetchers
    in.getShortArray(myCounters, 8);

    // The counter registers for the fractional data fetchers
    in.getIntArray(myFractionalCounters, 8);

    // The fractional registers for the data fetchers
    in.getByteArray(myFractionalIncrements, 8);

    // The Fast Fetcher Enabled flag
    myFastFetch = in.getBool();
    myLDAimmediate = in.getBool();

    // Control Byte to update
    in.getByteArray(myParameter, 8);

    // The music mode counters for the data fetchers
    in.getIntArray(myMusicCounters, 3);

    // The music mode frequency addends for the data fetchers
    in.getIntArray(myMusicFrequencies, 3);

    // The music waveforms
    in.getShortArray(myMusicWaveforms, 3);

    // The random number generator register
    myRandomNumber = in.getInt();

    // Get audio cycles and fractional clocks
    myAudioCycles = in.getLong();
    myFractionalClocks = in.getDouble();

    // Clock info for Thumbulator
    myARMCycles = in.getLong();
  }
  catch(...)
  {
    cerr << "ERROR: CartridgeDPCPlus::load" << endl;
    return false;
  }

  // Now, go to the current bank
  bank(myBankOffset >> 12);

  return true;
}
