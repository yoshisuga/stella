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
// Copyright (c) 1995-2017 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#ifndef CONFIG_PATH_DIALOG_HXX
#define CONFIG_PATH_DIALOG_HXX

class OSystem;
class GuiObject;
class DialogContainer;
class BrowserDialog;
class CheckboxWidget;
class PopUpWidget;
class EditTextWidget;
class SliderWidget;
class StaticTextWidget;

#include "Dialog.hxx"
#include "Command.hxx"

class ConfigPathDialog : public Dialog, public CommandSender
{
  public:
    ConfigPathDialog(OSystem& osystem, DialogContainer& parent,
                     const GUI::Font& font, GuiObject* boss);
    virtual ~ConfigPathDialog();

  private:
    void handleCommand(CommandSender* sender, int cmd, int data, int id) override;
    void createBrowser();

    void loadConfig() override;
    void saveConfig() override;
    void setDefaults() override;

  private:
    enum {
      kChooseRomDirCmd      = 'LOrm', // rom select
      kChooseStateDirCmd    = 'LOsd', // state dir
      kChooseCheatFileCmd   = 'LOcf', // cheatfile (stella.cht)
      kChoosePaletteFileCmd = 'LOpf', // palette file (stella.pal)
      kChoosePropsFileCmd   = 'LOpr', // properties file (stella.pro)
      kChooseNVRamDirCmd   =  'LOnv', // nvram (flash/eeprom) dir
      kStateDirChosenCmd    = 'LOsc', // state dir changed
      kCheatFileChosenCmd   = 'LOcc', // cheatfile changed
      kPaletteFileChosenCmd = 'LOpc', // palette file changed
      kPropsFileChosenCmd   = 'LOrc', // properties file changed
      kNVRamDirChosenCmd    = 'LOnc'  // nvram (flash/eeprom) dir changed
    };

    const GUI::Font& myFont;

    // Config paths
    EditTextWidget* myRomPath;
    EditTextWidget* myStatePath;
    EditTextWidget* myNVRamPath;
    EditTextWidget* myCheatFile;
    EditTextWidget* myPaletteFile;
    EditTextWidget* myPropsFile;

    unique_ptr<BrowserDialog> myBrowser;

    // Indicates if this dialog is used for global (vs. in-game) settings
    bool myIsGlobal;

  private:
    // Following constructors and assignment operators not supported
    ConfigPathDialog() = delete;
    ConfigPathDialog(const ConfigPathDialog&) = delete;
    ConfigPathDialog(ConfigPathDialog&&) = delete;
    ConfigPathDialog& operator=(const ConfigPathDialog&) = delete;
    ConfigPathDialog& operator=(ConfigPathDialog&&) = delete;
};

#endif
