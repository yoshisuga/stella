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

#ifndef TIA_TIA
#define TIA_TIA

#include <functional>

#include "bspf.hxx"
#include "ConsoleIO.hxx"
#include "ConsoleTiming.hxx"
#include "Settings.hxx"
#include "Device.hxx"
#include "Serializer.hxx"
#include "TIAConstants.hxx"
#include "DelayQueue.hxx"
#include "DelayQueueIterator.hxx"
#include "frame-manager/AbstractFrameManager.hxx"
#include "FrameLayout.hxx"
#include "Audio.hxx"
#include "Background.hxx"
#include "Playfield.hxx"
#include "Missile.hxx"
#include "Player.hxx"
#include "Ball.hxx"
#include "LatchedInput.hxx"
#include "PaddleReader.hxx"
#include "DelayQueueIterator.hxx"
#include "Control.hxx"
#include "System.hxx"

class AudioQueue;
class DispatchResult;

/**
  This class is a device that emulates the Television Interface Adaptor
  found in the Atari 2600 and 7800 consoles.  The Television Interface
  Adaptor is an integrated circuit designed to interface between an
  eight bit microprocessor and a television video modulator. It converts
  eight bit parallel data into serial outputs for the color, luminosity,
  and composite sync required by a video modulator.

  This class outputs the serial data into a frame buffer which can then
  be displayed on screen.

  @author  Christian Speckner (DirtyHairy) and Stephen Anthony
*/
class TIA : public Device
{
  public:
    /**
     * These dummy register addresses are used to represent the delayed
     * old / new register swap on writing GRPx and ENABL in the DelayQueue (see below).
     */
    enum DummyRegisters: uInt8 {
      shuffleP0 = 0xF0,
      shuffleP1 = 0xF1,
      shuffleBL = 0xF2
    };

    /**
     * Possible palette entries for objects in "fixed debug color mode".
     */
    enum FixedColor {
      NTSC_RED      = 0x30,
      NTSC_ORANGE   = 0x38,
      NTSC_YELLOW   = 0x1c,
      NTSC_GREEN    = 0xc4,
      NTSC_BLUE     = 0x9c,
      NTSC_PURPLE   = 0x66,
      NTSC_GREY     = 0x04,

      PAL_RED       = 0x62,
      PAL_ORANGE    = 0x4a,
      PAL_YELLOW    = 0x2e,
      PAL_GREEN     = 0x34,
      PAL_BLUE      = 0xbc,
      PAL_PURPLE    = 0xa6,
      PAL_GREY      = 0x06,

      SECAM_RED     = 0x04,
      SECAM_ORANGE  = 0x06, // purple
      SECAM_YELLOW  = 0x0c,
      SECAM_GREEN   = 0x08,
      SECAM_BLUE    = 0x02,
      SECAM_PURPLE  = 0x0a, // cyan
      SECAM_GREY    = 0x00,

      HBLANK_WHITE  = 0x0e
    };

    using ConsoleTimingProvider = std::function<ConsoleTiming()>;

  public:
    friend class TIADebug;
    friend class RiotDebug;

    /**
      Create a new TIA for the specified console

      @param console   The console the TIA is associated with
      @param settings  The settings object for this TIA device
    */
    TIA(ConsoleIO& console, ConsoleTimingProvider timingProvider, Settings& settings);

    virtual ~TIA() = default;

  public:
    /**
      Configure the frame manager.
     */
    void setFrameManager(AbstractFrameManager *frameManager);

    /**
      Set the audio queue. This needs to be dynamic as the queue is created after
      the timing has been determined.
    */
    void setAudioQueue(shared_ptr<AudioQueue> audioQueue);

    /**
      Clear the configured frame manager and deteach the lifecycle callbacks.
     */
    void clearFrameManager();

    /**
      Reset device to its power-on state.
    */
    void reset() override;

    /**
      Install TIA in the specified system.  Invoked by the system
      when the TIA is attached to it.

      @param system The system the device should install itself in
    */
    void install(System& system) override;

    /**
      Get the byte at the specified address.

      @return The byte at the specified address
    */
    uInt8 peek(uInt16 address) override;

    /**
      Change the byte at the specified address to the given value.

      @param address  The address where the value should be stored
      @param value    The value to be stored at the address

      @return  True if the poke changed the device address space, else false
    */
    bool poke(uInt16 address, uInt8 value) override;

    /**
      Install TIA in the specified system and device.  Invoked by
      the system when the TIA is attached to it.  All devices
      which invoke this method take responsibility for chaining
      requests back to *this* device.

      @param system  The system the device should install itself in
      @param device  The device responsible for this address space
    */
    void installDelegate(System& system, Device& device);

    /**
      Bind to controllers.
    */
    void bindToControllers();

    /**
      The following are very similar to save() and load(), except they
      do a 'deeper' save of the display data itself.

      Normally, the internal framebuffer doesn't need to be saved to
      a state file, since the file already contains all the information
      needed to re-create it, starting from scanline 0.  In effect, when a
      state is loaded, the framebuffer is empty, and the next call to
      update() generates valid framebuffer data.

      However, state files saved from the debugger need more information,
      such as the exact state of the internal framebuffer itself *before*
      we call update(), including if the display was in partial frame mode.

      Essentially, a normal state save has 'frame resolution', whereas
      the debugger state save has 'cycle resolution', and hence needs
      more information.  The methods below save/load this extra info,
      and eliminate having to save approx. 50K to normal state files.
    */
    bool saveDisplay(Serializer& out) const;
    bool loadDisplay(Serializer& in);

    /**
      This method should be called at an interval corresponding to the
      desired frame rate to update the TIA.  Invoking this method will update
      the graphics buffer and generate the corresponding audio samples.
    */
    void update(DispatchResult& result, uInt64 maxCycles = 50000);

    void update(uInt64 maxCycles = 50000);

    /**
      Did we generate a new frame?
     */
    bool newFramePending() { return myFramesSinceLastRender  > 0; }

    /**
     * Clear any pending frames.
     */
    void clearPendingFrame() { myFramesSinceLastRender = 0; }

    /**
      The number of frames since we did last render to the front buffer.
     */
    uInt32 framesSinceLastRender() { return myFramesSinceLastRender; }

    /**
      Render the pending frame to the framebuffer and clear the flag.
     */
    void renderToFrameBuffer();

    /**
      Return the buffer that holds the currently drawing TIA frame
      (the TIA output widget needs this).
     */
    uInt8* outputBuffer() { return myBackBuffer; }

    /**
      Returns a pointer to the internal frame buffer.
    */
    uInt8* frameBuffer() { return static_cast<uInt8*>(myFramebuffer); }

    /**
      Answers dimensional info about the framebuffer.
    */
    uInt32 width() const  { return TIAConstants::H_PIXEL; }
    uInt32 height() const { return myFrameManager->height(); }
    uInt32 ystart() const { return myFrameManager->ystart(); }

    /**
      Changes the current YStart property.
    */
    void setYStart(uInt32 ystart) { myFrameManager->setYstart(ystart); }

    void setLayout(FrameLayout layout) { myFrameManager->setLayout(layout); }
    FrameLayout frameLayout() const { return myFrameManager->layout(); }

    /**
      Enables/disables color-loss for PAL modes only.

      @param enabled  Whether to enable or disable PAL color-loss mode
    */
    bool enableColorLoss(bool enabled);

    /**
      Answers whether color-loss is enabled.

      @return  Colour-loss is enabled
    */
    bool colorLossEnabled() const { return myColorLossEnabled; }

    /**
      Answers whether colour-loss is applicable for the current frame.

      @return  Colour-loss is active for this frame
    */
    bool colorLossActive() const { return myColorLossActive; }

    /**
      Answers the current color clock we've gotten to on this scanline.

      @return The current color clock
    */
    uInt32 clocksThisLine() const { return myHctr - myHctrDelta; }

    /**
      Answers the total number of scanlines the TIA generated in producing
      the current frame buffer. For partial frames, this will be the
      current scanline.

      @return The total number of scanlines generated
    */
    uInt32 scanlines() const { return myFrameManager->scanlines(); }

    /**
      Answers the total number of scanlines the TIA generated in the
      previous frame.

      @return The total number of scanlines generated in the last frame.
    */
    uInt32 scanlinesLastFrame() const { return myFrameManager->scanlinesLastFrame(); }

    /**
      The same, but for the frame in the frame buffer.
     */
    uInt32 frameBufferScanlinesLastFrame() const { return myFrameBufferScanlines; }

    /**
      Answers the total system cycles from the start of the emulation.
    */
    uInt64 cycles() const { return uInt64(mySystem->cycles()); }

    /**
      Answers the frame count from the start of the emulation.
    */
    uInt32 frameCount() const { return myFrameManager->frameCount(); }

    /**
      Answers the system cycles from the start of the current frame.
    */
    uInt32 frameCycles() const {
      return uInt32(mySystem->cycles() - myCyclesAtFrameStart);
    }

    /**
      Answers whether the TIA is currently in being rendered
      (we're in between the start and end of drawing a frame).

      @return If the frame is in rendering mode
    */
    bool isRendering() const { return myFrameManager->isRendering(); }

    /**
      Answers the current position of the virtual 'electron beam' used
      when drawing the TIA image in debugger mode.

      @return The x/y coordinates of the scanline electron beam, and whether
              it is in the visible/viewable area of the screen
    */
    bool electronBeamPos(uInt32& x, uInt32& y) const;

    /**
      Enables/disable/toggle the specified (or all) TIA bit(s).  Note that
      disabling a graphical object also disables its collisions.

      @param mode  1/0 indicates on/off, and values greater than 1 mean
                   flip the bit from its current state

      @return  Whether the bit was enabled or disabled
    */
    bool toggleBit(TIABit b, uInt8 mode = 2);
    bool toggleBits();

    /**
      Enables/disable/toggle the specified (or all) TIA bit collision(s).

      @param mode  1/0 indicates on/off, and values greater than 1 mean
                   flip the collision from its current state

      @return  Whether the collision was enabled or disabled
    */
    bool toggleCollision(TIABit b, uInt8 mode = 2);
    bool toggleCollisions();

    /**
      Enables/disable/toggle/query 'fixed debug colors' mode.

      @param enable  Whether to enable fixed debug colors mode

      @return  Whether the mode was enabled or disabled
    */
    bool enableFixedColors(bool enable);
    bool toggleFixedColors() { return enableFixedColors(!usingFixedColors()); }
    bool usingFixedColors() const { return myColorHBlank != 0x00; }

    /**
      Sets the color of each object in 'fixed debug colors' mode.
      Note that this doesn't enable/disable fixed colors; it simply
      updates the palette that is used.

      @param colors  Each character in the 6-char string represents the
                     first letter of the color to use for
                     P0/M0/P1/M1/PF/BL, respectively.

      @return  True if colors were changed successfully, else false
    */
    bool setFixedColorPalette(const string& colors);

    /**
      Enable/disable/query state of 'undriven/floating TIA pins'.

      @param mode  1/0 indicates on/off, otherwise return the current state

      @return  Whether the mode was enabled or disabled
    */
    bool driveUnusedPinsRandom(uInt8 mode = 2);

    /**
      Enables/disable/toggle 'scanline jittering' mode, and set the
      recovery 'factor'.

      @param mode  1/0 indicates on/off, otherwise flip from
                   its current state

      @return  Whether the mode was enabled or disabled
    */
    bool toggleJitter(uInt8 mode = 2);
    void setJitterRecoveryFactor(Int32 factor);

    /**
      Enables/disables delayed playfield bits values.

      @param delayed   Wether to enable delayed playfield delays
    */
    void setPFBitsDelay(bool delayed);

    /**
      Enables/disables delayed playfield colors.

      @param delayed   Wether to enable delayed playfield colors
    */
    void setPFColorDelay(bool delayed);

    /**
      Enables/disables delayed player swapping.

      @param delayed   Wether to enable delayed player swapping
    */
    void setPlSwapDelay(bool delayed);

    /**
      Enables/disables delayed ball swapping.

      @param delayed   Wether to enable delayed ball swapping
    */
    void setBlSwapDelay(bool delayed);

    /**
      Enables/disables inverted HMOVE phase clock for players.

      @param enable   Wether to enable inverted HMOVE phase clock for players
    */
    void setPlInvertedPhaseClock(bool enable);

    /**
      Enables/disables inverted HMOVE phase clock for missiles.

      @param enable   Wether to enable inverted HMOVE phase clock for missiles
    */
    void setMsInvertedPhaseClock(bool enable);

    /**
      Enables/disables inverted HMOVE phase clock for ball.

      @param enable   Wether to enable inverted HMOVE phase clock for ball
    */
    void setBlInvertedPhaseClock(bool enable);

    /**
      This method should be called to update the TIA with a new scanline.
    */
    TIA& updateScanline();

    /**
      This method should be called to update the TIA with a new partial
      scanline by stepping one CPU instruction.
    */
    TIA& updateScanlineByStep();

    /**
      Retrieve the last value written to a certain register.
    */
    uInt8 registerValue(uInt8 reg) const;

    /**
      Get the current x value.
    */
    uInt8 getPosition() const {
      uInt8 realHctr = myHctr - myHctrDelta;

      return (realHctr < TIAConstants::H_BLANK_CLOCKS) ? 0 : (realHctr - TIAConstants::H_BLANK_CLOCKS);
    }

    /**
      Flush the line cache after an externally triggered state change
      (e.g. a register write).
    */
    void flushLineCache();

    /**
      Schedule a collision update
     */
    void scheduleCollisionUpdate();

    /**
      Create a new delayQueueIterator for the debugger.
    */
    shared_ptr<DelayQueueIterator> delayQueueIterator() const;

    /**
      Save the current state of this device to the given Serializer.

      @param out  The Serializer object to use
      @return  False on any errors, else true
    */
    bool save(Serializer& out) const override;

    /**
      Load the current state of this device from the given Serializer.

      @param in  The Serializer object to use
      @return  False on any errors, else true
    */
    bool load(Serializer& in) override;

    /**
     * Run and forward TIA emulation to the current system clock.
     */
    void updateEmulation();

  private:
    /**
     * During each line, the TIA cycles through these two states.
     */
    enum class HState {blank, frame};

    /**
     * The three different modes of the priority encoder. Check TIA::renderPixel
     * for a precise definition.
     */
    enum class Priority {pfp, score, normal};

    /**
     * Palette and indices for fixed debug colors.
     */
    enum FixedObject { P0, M0, P1, M1, PF, BL, BK };
    FixedColor myFixedColorPalette[3][7];
    string myFixedColorNames[7];

  private:
    /**
     * This callback is invoked by FrameManager when a new frame starts.
     */
    void onFrameStart();

    /**
     * This callback is invoked by FrameManager when the current frame completes.
     */
    void onFrameComplete();

    /**
     * Called when the CPU enters halt state (RDY pulled low). Execution continues
     * immediatelly afterwards, so we have to adjust the system clock to account
     * for the cycles the 6502 spent in halt state.
     */
    void onHalt();

    /**
     * Execute colorClocks cycles of TIA simulation.
     */
    void cycle(uInt32 colorClocks);

    /**
     * Advance the movement logic by a single clock.
     */
    void tickMovement();

    /**
     * Advance a single clock during hblank.
     */
    void tickHblank();

    /**
     * Advance a single clock duing the visible part of the scanline.
     */
    void tickHframe();

    /**
     * Update the collision bitfield.
     */
    void updateCollision();

    /**
     * Execute a RSYNC.
     */
    void applyRsync();

    /**
     * Render the current pixel into the framebuffer.
     */
    void renderPixel(uInt32 x, uInt32 y);

    /**
     * Clear the first 8 pixels of a scanline with black if we are in hblank
     * (called during HMOVE).
     */
    void clearHmoveComb();

    /**
     * Advance a line and update our state accordingly.
     */
    void nextLine();

    /**
     * Clone the last line. Called in nextLine if TIA state was unchanged.
     */
    void cloneLastLine();

    /**
     * Execute a delayed write. Called when the DelayQueue is pumped.
     */
    void delayedWrite(uInt8 address, uInt8 value);

    /**
     * Update all paddle readout circuits to the current controller state.
     */
    void updatePaddle(uInt8 idx);

    /**
     * Get the target counter value during a RESx. This essentially depends on
     * the position in the current scanline.
     */
    uInt8 resxCounter();

    /**
     * Get the result of the specified collision register.
     */
    uInt8 collCXM0P() const;
    uInt8 collCXM1P() const;
    uInt8 collCXP0FB() const;
    uInt8 collCXP1FB() const;
    uInt8 collCXM0FB() const;
    uInt8 collCXM1FB() const;
    uInt8 collCXPPMM() const;
    uInt8 collCXBLPF() const;

    /**
     * Toggle the specified collision bits
     */
    void toggleCollP0PF();
    void toggleCollP0BL();
    void toggleCollP0M1();
    void toggleCollP0M0();
    void toggleCollP0P1();
    void toggleCollP1PF();
    void toggleCollP1BL();
    void toggleCollP1M1();
    void toggleCollP1M0();
    void toggleCollM0PF();
    void toggleCollM0BL();
    void toggleCollM0M1();
    void toggleCollM1PF();
    void toggleCollM1BL();
    void toggleCollBLPF();

    /**
     * Re-apply developer settings from the settings object.
     * This should be done each time the device is reset, or after
     * a state load occurs.
     */
    void applyDeveloperSettings();

  #ifdef DEBUGGER_SUPPORT
    void createAccessBase();

    /**
     * Query the given address type for the associated disassembly flags.
     *
     * @param address  The address to query
     */
    uInt8 getAccessFlags(uInt16 address) const override;
    /**
     * Change the given address to use the given disassembly flags.
     *
     * @param address  The address to modify
     * @param flags    A bitfield of DisasmType directives for the given address
     */
    void setAccessFlags(uInt16 address, uInt8 flags) override;
  #endif // DEBUGGER_SUPPORT

  private:
    ConsoleIO& myConsole;
    ConsoleTimingProvider myTimingProvider;
    Settings& mySettings;

    /**
     * The length of the delay queue (maximum number of clocks delay)
     */
    static constexpr unsigned delayQueueLength = 16;

    /**
     * The size of the delay queue (maximum number of writes scheduled in a single slot).
     */
    static constexpr unsigned delayQueueSize = 16;

    /**
     * A list of delayed writes that are queued up for future execution. Delayed
     * writes can be both actual writes whose effect is delayed by one or more clocks
     * on real hardware and delayed side effects of certain operations (GRPx!).
     */
    DelayQueue<delayQueueLength, delayQueueSize> myDelayQueue;

    /**
      Variable delay values for TIA writes.
    */
    uInt8 myPFBitsDelay;
    uInt8 myPFColorDelay;
    uInt8 myPlSwapDelay;
    uInt8 myBlSwapDelay;

    /**
     * The frame manager is responsible for detecting frame boundaries and the visible
     * region of each frame.
     */
    AbstractFrameManager* myFrameManager;

    /**
     * The various TIA objects.
     */
    Background myBackground;
    Playfield myPlayfield;
    Missile myMissile0;
    Missile myMissile1;
    Player myPlayer0;
    Player myPlayer1;
    Ball myBall;

    Audio myAudio;

    /**
     * The paddle readout circuits.
     */
    PaddleReader myPaddleReaders[4];

    /**
     * Circuits for the "latched inputs".
     */
    LatchedInput myInput0;
    LatchedInput myInput1;

    // Pointer to the internal color-index-based frame buffer
    uInt8 myFramebuffer[TIAConstants::H_PIXEL * TIAConstants::frameBufferHeight];

    // The frame is rendered to the backbuffer and only copied to the framebuffer
    // upon completion
    uInt8 myBackBuffer[TIAConstants::H_PIXEL * TIAConstants::frameBufferHeight];
    uInt8 myFrontBuffer[TIAConstants::H_PIXEL * TIAConstants::frameBufferHeight];

    // We snapshot frame statistics when the back buffer is copied to the front buffer
    // and when the front buffer is copied to the frame buffer
    uInt32 myFrontBufferScanlines, myFrameBufferScanlines;

    // Frames since the last time a frame was rendered to the render buffer
    uInt32 myFramesSinceLastRender;

    /**
     * Setting this to true injects random values into undefined reads.
     */
    bool myTIAPinsDriven;

    /**
     * The current "line state" --- either hblank or frame.
     */
    HState myHstate;

    /**
     * Master line counter
     */
    uInt8 myHctr;

    /**
     * Delta between master line counter and actual color clock. Nonzero after
     * RSYNC (before the scanline terminates)
     */
    Int32 myHctrDelta;

    /**
     * Electron beam x at rendering start (used for blanking out any pixels from
     * the last frame that are not overwritten)
     */
    uInt8 myXAtRenderingStart;

    /**
     * Do we need to update the collision mask this clock?
     */
    bool myCollisionUpdateRequired;

    /**
     * Force schedule a collision update
     */
    bool myCollisionUpdateScheduled;

    /**
     * The collision latches are represented by 15 bits in a bitfield.
     */
    uInt32 myCollisionMask;

    /**
     * The movement clock counts the extra ticks sent to the objects during
     * movement.
     */
    uInt32 myMovementClock;

    /**
     * Movement mode --- are we sending movement clocks?
     */
    bool myMovementInProgress;

    /**
     * Do we have an extended hblank this line? Get set by strobing HMOVE and
     * cleared when the line wraps.
     */
    bool myExtendedHblank;

    /**
     * Counts the number of line wraps since the last external TIA state change.
     * If at least two line breaks have passed, the TIA will suspend simulation
     * and just reuse the last line instead.
     */
    uInt32 myLinesSinceChange;

    /**
     * The current mode of the priority encoder.
     */
    Priority myPriority;

    /**
     * The index of the last CPU cycle that was included in the simulation.
     */
    uInt64 myLastCycle;

    /**
     * Keeps track of a possible fractional number of clocks that still need
     * to be simulated.
     */
    uInt8 mySubClock;

    /**
     * Bitmasks that track which sprites / collisions are enabled / disabled.
     */
    uInt8 mySpriteEnabledBits;
    uInt8 myCollisionsEnabledBits;

    /**
     * The color used to highlight HMOVE blanks (if enabled).
     */
    uInt8 myColorHBlank;

    /**
     * The total number of color clocks since emulation started.
     */
    uInt64 myTimestamp;

    /**
     * The "shadow registers" track the last written register value for the
     * debugger.
     */
    uInt8 myShadowRegisters[64];

    /**
     * Indicates if color loss should be enabled or disabled.  Color loss
     * occurs on PAL-like systems when the previous frame contains an odd
     * number of scanlines.
     */
    bool myColorLossEnabled;
    bool myColorLossActive;

    /**
     * System cycles at the end of the previous frame / beginning of next frame.
     */
    uInt64 myCyclesAtFrameStart;

    /**
     * The frame manager can change during our lifetime, so we buffer those two.
     */
    bool myEnableJitter;
    uInt8 myJitterFactor;

  #ifdef DEBUGGER_SUPPORT
    // The arrays containing information about every byte of TIA
    // indicating whether and how (RW) it is used.
    ByteBuffer myAccessBase;

    // The array used to skip the first two TIA access trackings
    ByteBuffer myAccessDelay;
  #endif // DEBUGGER_SUPPORT

    static constexpr uInt16
      TIA_SIZE = 0x40, TIA_MASK = TIA_SIZE - 1, TIA_READ_MASK = 0x0f, TIA_BIT = 0x080, TIA_DELAY = 2;

  private:
    TIA() = delete;
    TIA(const TIA&) = delete;
    TIA(TIA&&) = delete;
    TIA& operator=(const TIA&) = delete;
    TIA& operator=(TIA&&) = delete;
};

#endif // TIA_TIA
